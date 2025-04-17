#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <mosquitto.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

class MqttClient {
public:
    MqttClient();
    ~MqttClient();
    
    bool connect(const std::string& clientId, 
                 const std::string& host = "localhost", 
                 int port = 1883, 
                 const std::string& username = "", 
                 const std::string& password = "");
    
    void disconnect();
    
    bool publish_image(const std::string& topic, const std::string& image_path, 
                       const std::string& window_title = "", 
                       const std::string& capture_type = "",
                       bool retain = false);
    
    bool is_connected() const { return connected_; }
    
    std::string base64_encode(const std::vector<char>& input);
    
private:
    struct mosquitto* mosquitto_;
    std::string host_;
    int port_;
    bool connected_;
    std::atomic<bool> running_;
    std::thread loop_thread_;
    std::mutex mqtt_mutex_;
    
    void start_loop();
    void stop_loop();
    void loop_worker();
    
    // Callback handlers
    void on_connect(int rc);
    void on_disconnect(int rc);
    void on_publish(int mid);
    
    // Static callback wrappers
    static void on_connect_wrapper(struct mosquitto* mosq, void* obj, int rc);
    static void on_disconnect_wrapper(struct mosquitto* mosq, void* obj, int rc);
    static void on_publish_wrapper(struct mosquitto* mosq, void* obj, int mid);
    
    std::string get_current_timestamp();
    std::string escape_json_string(const std::string& input);
};

#endif // MQTT_CLIENT_H