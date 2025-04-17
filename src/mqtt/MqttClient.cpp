#include "../include/MqttClient.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

MqttClient::MqttClient() : mosquitto_(nullptr), connected_(false), running_(false) {
    // Initialize mosquitto library
    mosquitto_lib_init();
}

MqttClient::~MqttClient() {
    disconnect();
    
    // Free resources
    if (mosquitto_) {
        mosquitto_destroy(mosquitto_);
    }
    
    // Cleanup mosquitto library
    mosquitto_lib_cleanup();
}

bool MqttClient::connect(const std::string& clientId, 
                        const std::string& host, 
                        int port, 
                        const std::string& username, 
                        const std::string& password) {
    
    // Create a new mosquitto client instance
    if (mosquitto_) {
        mosquitto_destroy(mosquitto_);
    }
    
    mosquitto_ = mosquitto_new(clientId.c_str(), true, this);
    if (!mosquitto_) {
        std::cerr << "❌ Failed to create MQTT client instance" << std::endl;
        return false;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mosquitto_, on_connect_wrapper);
    mosquitto_disconnect_callback_set(mosquitto_, on_disconnect_wrapper);
    mosquitto_publish_callback_set(mosquitto_, on_publish_wrapper);
    
    // Set credentials if provided
    if (!username.empty()) {
        mosquitto_username_pw_set(mosquitto_, username.c_str(), password.c_str());
    }
    
    // Connect to the broker
    int result = mosquitto_connect(mosquitto_, host.c_str(), port, 60);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ Failed to connect to MQTT broker: " << mosquitto_strerror(result) << std::endl;
        return false;
    }
    
    // Start the network loop in a background thread
    start_loop();
    
    host_ = host;
    port_ = port;
    std::cout << "🔌 MQTT client connecting to " << host << ":" << port << std::endl;
    
    return true;
}

void MqttClient::disconnect() {
    // Stop the network loop
    stop_loop();
    
    // Disconnect from the broker
    if (mosquitto_ && connected_) {
        mosquitto_disconnect(mosquitto_);
        connected_ = false;
        std::cout << "🔌 MQTT client disconnected" << std::endl;
    }
}

bool MqttClient::publish_image(const std::string& topic, const std::string& image_path, 
                              const std::string& window_title, 
                              const std::string& capture_type,
                              bool retain) {
    if (!mosquitto_ || !connected_) {
        std::cerr << "❌ MQTT client not connected" << std::endl;
        return false;
    }
    
    // Read file content
    std::ifstream file(image_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "❌ Failed to open image file: " << image_path << std::endl;
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        std::cerr << "❌ Failed to read image file: " << image_path << std::endl;
        return false;
    }

    // Base64 encode the image
    std::string base64_image = base64_encode(buffer);

    // Build metadata JSON with encoded image
    std::string filename = image_path.substr(image_path.find_last_of('/') + 1);
    std::ostringstream json;
    json << "{";
    json << "\"filename\":\"" << filename << "\",";
    json << "\"timestamp\":\"" << get_current_timestamp() << "\",";
    json << "\"source\":\"sauron\",";
    json << "\"format\":\"" << filename.substr(filename.find_last_of('.') + 1) << "\",";
    if (!window_title.empty()) json << "\"window_title\":\"" << escape_json_string(window_title) << "\",";
    if (!capture_type.empty()) json << "\"capture_type\":\"" << capture_type << "\",";
    json << "\"encoding\":\"base64\",";
    json << "\"data\":\"" << base64_image << "\"";
    json << "}";
    std::string payload = json.str();

    int mid = 0;
    int result = mosquitto_publish(mosquitto_, &mid, topic.c_str(), payload.size(), payload.c_str(), 0, retain);
    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ Failed to publish image JSON: " << mosquitto_strerror(result) << std::endl;
        return false;
    }

    std::cout << "📤 Published image JSON to " << topic << ", message ID " << mid << std::endl;
    return true;
}

void MqttClient::start_loop() {
    if (running_) {
        return;
    }
    
    running_ = true;
    loop_thread_ = std::thread(&MqttClient::loop_worker, this);
}

void MqttClient::stop_loop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

void MqttClient::loop_worker() {
    while (running_) {
        std::lock_guard<std::mutex> lock(mqtt_mutex_);
        mosquitto_loop(mosquitto_, 100, 1);
    }
}

void MqttClient::on_connect(int rc) {
    if (rc == 0) {
        connected_ = true;
        std::cout << "✅ Connected to MQTT broker at " << host_ << ":" << port_ << std::endl;
    } else {
        std::cerr << "❌ Failed to connect to MQTT broker: " << mosquitto_connack_string(rc) << std::endl;
    }
}

void MqttClient::on_disconnect(int rc) {
    connected_ = false;
    
    if (rc == 0) {
        std::cout << "👋 Disconnected from MQTT broker" << std::endl;
    } else {
        std::cerr << "⚠️ Unexpected disconnection from MQTT broker: " << rc << std::endl;
    }
}

void MqttClient::on_publish(int mid) {
    std::cout << "✅ Message with ID " << mid << " published successfully" << std::endl;
}

void MqttClient::on_connect_wrapper(struct mosquitto* mosq, void* obj, int rc) {
    MqttClient* client = static_cast<MqttClient*>(obj);
    if (client) {
        client->on_connect(rc);
    }
}

void MqttClient::on_disconnect_wrapper(struct mosquitto* mosq, void* obj, int rc) {
    MqttClient* client = static_cast<MqttClient*>(obj);
    if (client) {
        client->on_disconnect(rc);
    }
}

void MqttClient::on_publish_wrapper(struct mosquitto* mosq, void* obj, int mid) {
    MqttClient* client = static_cast<MqttClient*>(obj);
    if (client) {
        client->on_publish(mid);
    }
}

std::string MqttClient::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
    
    return ss.str();
}

std::string MqttClient::escape_json_string(const std::string& input) {
    std::string output;
    output.reserve(input.length() + 10); // Reserve some extra space
    
    for (char c : input) {
        switch (c) {
            case '\"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    // Escape control characters
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (int)c);
                    output += buf;
                } else {
                    output += c;
                }
        }
    }
    
    return output;
}

std::string MqttClient::base64_encode(const std::vector<char>& input) {
    if (input.empty()) {
        return "";
    }
    
    // Ensure input size is within reasonable limits
    if (input.size() > 20 * 1024 * 1024) { // 20MB limit
        std::cerr << "❌ Input size too large for Base64 encoding" << std::endl;
        return "";
    }
    
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    // Do not use newlines in the output - important for JSON embedding
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    
    BIO_write(bio, input.data(), static_cast<int>(input.size()));
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    std::string result(bufferPtr->data, bufferPtr->length);
    
    BIO_free_all(bio);
    
    return result;
}