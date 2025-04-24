#include "../../include/MqttClient.h"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <fstream>
#include <iostream>  // Added include for std::cout, std::cerr, std::endl
#include <sstream> // Add this include for std::stringstream
#include <vector>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp> // Add this include for JSON manipulation

MqttClient::MqttClient() : mosq_(nullptr), connected_(false) {
    mosquitto_lib_init();
    mosq_ = mosquitto_new(nullptr, true, this);
    if (mosq_) {
        mosquitto_connect_callback_set(mosq_, on_connect_callback);
        mosquitto_disconnect_callback_set(mosq_, on_disconnect_callback);
    }
}

MqttClient::~MqttClient() {
    if (mosq_) {
        if (connected_) {
            mosquitto_disconnect(mosq_);
        }
        mosquitto_destroy(mosq_);
    }
    mosquitto_lib_cleanup();
}

bool MqttClient::connect(const std::string& host, const std::string& username, int port) {
    if (!mosq_) return false;
    
    // Set username if provided
    if (!username.empty()) {
        mosquitto_username_pw_set(mosq_, username.c_str(), nullptr);
    }
    
    std::cout << "Connecting to MQTT broker at host: " << host << ", port: " << port << std::endl;

    if (host.empty()) {
        std::cerr << "❌ MQTT connection error: Hostname is empty" << std::endl;
        return false;
    }

    int rc = mosquitto_connect(mosq_, host.c_str(), port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ MQTT connection error: " << mosquitto_strerror(rc) << std::endl;
        return false;
    }

    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ MQTT loop start error: " << mosquitto_strerror(rc) << std::endl;
        return false;
    }

    std::cout << "✅ MQTT connected to " << host << ":" << port << std::endl;
    return true;
}

void MqttClient::disconnect() {
    if (mosq_ && connected_) {
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, true);
        connected_ = false;
    }
}

void MqttClient::on_connect_callback(struct mosquitto* mosq [[maybe_unused]], void* obj, int rc) {
    MqttClient* client = static_cast<MqttClient*>(obj);
    client->connected_ = (rc == 0);
}

void MqttClient::on_disconnect_callback(struct mosquitto* mosq [[maybe_unused]], void* obj, int rc [[maybe_unused]]) {
    MqttClient* client = static_cast<MqttClient*>(obj);
    client->connected_ = false;
}

void MqttClient::on_message_callback(struct mosquitto* mosq [[maybe_unused]], void* obj, const struct mosquitto_message* message) {
    MqttClient* client = static_cast<MqttClient*>(obj);
    if (client && client->message_callback_) {
        std::string topic(message->topic);
        std::string payload(static_cast<char*>(message->payload), message->payloadlen);
        client->message_callback_(topic, payload);
    }
}

void MqttClient::set_message_callback(MessageCallback callback) {
    message_callback_ = callback;
    if (mosq_) {
        mosquitto_message_callback_set(mosq_, on_message_callback);
    }
}

bool MqttClient::subscribe(const std::string& topic) {
    if (!mosq_ || !connected_) {
        std::cerr << "Cannot subscribe: MQTT client not connected" << std::endl;
        return false;
    }
    
    try {
        mosquitto_subscribe(mosq_, nullptr, topic.c_str(), 0); // QoS level 0
        std::cout << "Subscribed to topic: " << topic << std::endl;
        return true;
    } catch (const std::exception& exc) {
        std::cerr << "Error subscribing to topic " << topic << ": " << exc.what() << std::endl;
        return false;
    }
}

bool MqttClient::publish(const std::string& topic, const std::string& message) {
    if (!mosq_ || !connected_) {
        std::cerr << "Cannot publish: MQTT client not connected" << std::endl;
        return false;
    }
    
    try {
        int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(), 
                                   message.length(), message.c_str(), 
                                   0, false);
        if (rc == MOSQ_ERR_SUCCESS) {
            std::cout << "Published message to topic " << topic << std::endl;
            return true;
        } else {
            std::cerr << "Error publishing to topic " << topic << ": " << mosquitto_strerror(rc) << std::endl;
            return false;
        }
    } catch (const std::exception& exc) {
        std::cerr << "Error publishing to topic " << topic << ": " << exc.what() << std::endl;
        return false;
    }
}

bool MqttClient::publish_image(const std::string& topic, const std::string& filename,
                             const std::string& routing_info, // Renamed parameter for clarity
                             const std::string& trigger_type,
                             bool as_base64 [[maybe_unused]]) { // as_base64 is effectively always true now
    if (!mosq_ || !connected_) {
        std::cerr << "❌ Cannot publish: MQTT client not connected" << std::endl;
        return false;
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "❌ Failed to open image file: " << filename << std::endl;
        return false;
    }

    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
    
    std::string base64_payload;
    // Always encode image data as Base64
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // Avoid newlines
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_write(bio, buffer.data(), buffer.size());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    base64_payload.assign(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    // Parse routing_info string (e.g., "to:agent,from:ui,type:image")
    nlohmann::json msg_json;
    std::stringstream ss_routing(routing_info);
    std::string segment;
    while (std::getline(ss_routing, segment, ',')) {
        size_t colon_pos = segment.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = segment.substr(0, colon_pos);
            std::string value = segment.substr(colon_pos + 1);
            msg_json[key] = value; // Add routing info as top-level fields
        }
    }

    // Add metadata and image data
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char tsbuf[20];
    std::strftime(tsbuf, sizeof(tsbuf), "%FT%TZ", std::gmtime(&t));
    
    msg_json["filename"] = std::filesystem::path(filename).filename().string();
    msg_json["trigger_type"] = trigger_type;
    msg_json["timestamp"] = tsbuf;
    msg_json["image_data"] = base64_payload; // Add base64 image data

    // Add a placeholder for the actual window title if needed, or remove if unused by agent
    // msg_json["window_title"] = "Actual Window Title"; // Example if needed

    std::string message = msg_json.dump(); // Serialize the JSON object

    int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(), 
                             message.length(), message.c_str(), 
                             0, false);
    
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "❌ Error publishing to topic " << topic << ": " << mosquitto_strerror(rc) << std::endl;
        return false;
    }

    std::cout << "✅ Published image to topic " << topic << std::endl;
    return true;
}