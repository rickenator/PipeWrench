#include "../include/OllamaBackend.h"
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

OllamaBackend::OllamaBackend() {
    // Initialize cURL globally (only once)
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
    }
}

OllamaBackend::~OllamaBackend() {
    // Note: We don't call curl_global_cleanup() here because it should only be called once at program exit
}

bool OllamaBackend::initialize(const std::string& api_key, 
                             const std::string& api_host,
                             const std::string& model_name) {
    // Ollama doesn't use API keys, so we ignore that parameter
    (void)api_key;
    
    api_host_ = api_host;
    model_name_ = model_name;
    
    // Validate parameters
    if (api_host_.empty()) {
        api_host_ = "http://localhost:11434"; // Default Ollama endpoint
    }
    
    if (model_name_.empty()) {
        model_name_ = "llama3"; // Default model
    }
    
    // Test the connection to Ollama
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "❌ Failed to initialize cURL" << std::endl;
        return false;
    }
    
    // Build the API endpoint URL to check model list
    std::string url = api_host_ + "/api/tags";
    
    // Response data
    std::string response_data;
    
    // Set up cURL options
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5); // 5 seconds timeout
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "❌ Failed to connect to Ollama at " << api_host_ << ": " 
                 << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    // Check if the model exists
    bool model_exists = false;
    try {
        json response_json = json::parse(response_data);
        if (response_json.contains("models")) {
            for (const auto& model : response_json["models"]) {
                if (model.contains("name") && model["name"] == model_name_) {
                    model_exists = true;
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Error parsing Ollama response: " << e.what() << std::endl;
        // We'll continue anyway, as the model might still work
    }
    
    if (!model_exists) {
        std::cout << "⚠️ Model '" << model_name_ << "' not found in Ollama. "
                 << "It will be pulled on first use." << std::endl;
    }
    
    initialized_ = true;
    return true;
}

bool OllamaBackend::is_ready() const {
    return initialized_.load();
}

std::string OllamaBackend::prepare_request_payload(const std::vector<Message>& messages, const std::string& image_path) {
    json payload;
    payload["model"] = model_name_;
    
    // Format the prompt for Ollama
    // Ollama uses a simpler format than OpenAI
    std::string formatted_prompt;
    
    for (const auto& msg : messages) {
        std::string role_prefix;
        
        switch (msg.role) {
            case Message::Role::SYSTEM:
                role_prefix = "System: ";
                break;
            case Message::Role::USER:
                role_prefix = "User: ";
                break;
            case Message::Role::ASSISTANT:
                role_prefix = "Assistant: ";
                break;
            default:
                role_prefix = "User: ";
                break;
        }
        
        formatted_prompt += role_prefix + msg.content + "\n\n";
    }
    
    // Add prompt instruction to continue as assistant
    formatted_prompt += "Assistant: ";
    
    // Add image if provided
    if (!image_path.empty()) {
        std::string base64_image;
        if (encode_image_base64(image_path, base64_image)) {
            // Add image data to the payload if the model supports it
            // Note: Not all Ollama models support images, so check model capabilities
            if (model_name_.find("llava") != std::string::npos || 
                model_name_.find("bakllava") != std::string::npos) {
                payload["images"] = {base64_image};
            } else {
                std::cout << "⚠️ Model may not support images. Continuing with text only." << std::endl;
            }
        }
    }
    
    payload["prompt"] = formatted_prompt;
    
    // Add parameters
    payload["temperature"] = 0.7;
    payload["num_predict"] = 2048;
    payload["stream"] = false;
    
    // Convert to string
    return payload.dump();
}

bool OllamaBackend::encode_image_base64(const std::string& image_path, std::string& base64_output) {
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

bool OllamaBackend::send_message(const std::vector<Message>& messages,
                               const std::string& image_path,
                               ResponseCallback callback) {
    if (!is_ready()) {
        std::cerr << "❌ Ollama backend not initialized" << std::endl;
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
        std::string url = api_host_ + "/api/generate";
        
        // Set up HTTP headers
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        // Response data
        std::string response_data;
        
        // Set up cURL options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120); // 2 minutes timeout (local models can be slower)
        
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
                    response_text = "API Error: " + response_json["error"].get<std::string>();
                    has_error = true;
                } else if (response_json.contains("response")) {
                    response_text = response_json["response"].get<std::string>();
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
