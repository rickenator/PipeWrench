#include "../include/OpenAIBackend.h"
#include "../include/SauronAgent.h" // For Message struct
#include <iostream>
#include <sstream>
#include <fstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <thread>

using json = nlohmann::json;

// Callback function for CURL to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    output->append((char*)contents, total_size);
    return total_size;
}

OpenAIBackend::OpenAIBackend() {
    // Initialize cURL globally (only once)
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
    }
}

OpenAIBackend::~OpenAIBackend() {
    // Note: We don't call curl_global_cleanup() here because it should only be called once at program exit
}

bool OpenAIBackend::initialize(const std::string& api_key, 
                             const std::string& api_host,
                             const std::string& model_name) {
    api_key_ = api_key;
    api_host_ = api_host;
    model_name_ = model_name;
    
    // Validate parameters
    if (api_key_.empty()) {
        std::cerr << "❌ OpenAI API key is required" << std::endl;
        return false;
    }
    
    if (api_host_.empty()) {
        api_host_ = "https://api.openai.com/v1"; // Default endpoint
    }
    
    if (model_name_.empty()) {
        model_name_ = "gpt-4o"; // Default model
    }
    
    initialized_ = true;
    return true;
}

bool OpenAIBackend::is_ready() const {
    return initialized_.load();
}

std::string OpenAIBackend::prepare_request_payload(const std::vector<Message>& messages, const std::string& image_path) {
    json payload;
    payload["model"] = model_name_;
    payload["messages"] = json::array();

    for (const auto& msg : messages) {
        json message_obj;
        message_obj["role"] = msg.role_to_string();

        // Handle image content for the last user message if image_path is provided
        // Compare by ID instead of the whole object, and check if messages is not empty
        if (msg.role == Message::Role::USER && !image_path.empty() && !messages.empty() && msg.id == messages.back().id) {
            json content_array = json::array();
            
            // Add text part
            json text_part;
            text_part["type"] = "text";
            text_part["text"] = msg.content;
            content_array.push_back(text_part);

            // Add image part (assuming image_path is accessible and needs to be encoded)
            std::string image_url; // Declare image_url here
            if (encode_image_base64(image_path, image_url)) { // Correct function name and pass image_url by reference
                json image_part;
                image_part["type"] = "image_url";
                image_part["image_url"]["url"] = "data:image/png;base64," + image_url; // Assuming PNG, adjust if needed
                content_array.push_back(image_part);
            } else {
                 std::cerr << "Warning: Could not encode image: " << image_path << std::endl;
                 // Fallback to just text if encoding fails
                 message_obj["content"] = msg.content;
                 payload["messages"].push_back(message_obj);
                 continue; // Skip adding the array content below
            }
            
            message_obj["content"] = content_array;
        } else {
            // For other messages or user messages without images
            message_obj["content"] = msg.content;
        }

        payload["messages"].push_back(message_obj);
    }

    // Add max_tokens if needed (optional)
    // payload["max_tokens"] = 1000; 

    return payload.dump();
}

bool OpenAIBackend::encode_image_base64(const std::string& image_path, std::string& base64_output) {
    // Read the image file
    std::ifstream file(image_path, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Failed to open image file: " << image_path << std::endl;
        return false;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read file data
    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);
    file.close();
    
    // Base64 encode using OpenSSL
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, buffer.data(), file_size);
    BIO_flush(b64);
    
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    
    base64_output = std::string(bptr->data, bptr->length);
    
    BIO_free_all(b64);
    
    return true;
}

bool OpenAIBackend::send_message(const std::vector<Message>& messages,
                               const std::string& image_path,
                               ResponseCallback callback) {
    if (!is_ready()) {
        std::cerr << "❌ OpenAI backend not initialized" << std::endl;
        return false;
    }
    
    // Create the request payload
    std::string payload = prepare_request_payload(messages, image_path);
    
    // Create a new thread to handle the API request asynchronously
    std::thread request_thread([this, payload, callback]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            std::cerr << "❌ Failed to initialize cURL" << std::endl;
            callback("Error: Failed to initialize cURL", true);
            return;
        }
        
        // Build the API endpoint URL
        std::string url = api_host_;
        if (url.back() != '/') {
            url += '/';
        }
        url += "chat/completions";
        
        // Set up HTTP headers
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        std::string auth_header = "Authorization: Bearer " + api_key_;
        headers = curl_slist_append(headers, auth_header.c_str());
        
        // Response data
        std::string response_data;
        
        // Set up cURL options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30); // 30 seconds timeout
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        
        bool has_error = false;
        std::string response_text;
        
        if (res != CURLE_OK) {
            response_text = "Error: " + std::string(curl_easy_strerror(res));
            has_error = true;
        } else {
            // Parse the JSON response
            try {
                json response_json = json::parse(response_data);
                
                if (response_json.contains("error")) {
                    response_text = "API Error: " + response_json["error"]["message"].get<std::string>();
                    has_error = true;
                } else if (response_json.contains("choices") && !response_json["choices"].empty()) {
                    response_text = response_json["choices"][0]["message"]["content"].get<std::string>();
                } else {
                    response_text = "Error: Unexpected response format";
                    has_error = true;
                }
            } catch (const std::exception& e) {
                response_text = "Error parsing response: " + std::string(e.what());
                has_error = true;
            }
        }
        
        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        // Invoke callback with response
        callback(response_text, has_error);
    });
    
    // Detach the thread to run independently
    request_thread.detach();
    
    return true;
}
