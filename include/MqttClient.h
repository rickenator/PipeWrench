#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <mosquitto.h>
#include <string>
#include <memory>
#include <functional>

class MqttClient {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;
    
    MqttClient();
    ~MqttClient();

    bool connect(const std::string& host, const std::string& username, int port = 1883);
    void disconnect();
    bool is_connected() const { return connected_; }
    bool publish(const std::string& topic, const std::string& message);
    bool publish_image(const std::string& topic, const std::string& filename, 
                      const std::string& window_title, const std::string& trigger_type,
                      bool as_base64 = false);
    void set_message_callback(MessageCallback callback);
    bool subscribe(const std::string& topic);

private:
    struct mosquitto* mosq_;
    bool connected_;
    MessageCallback message_callback_;
    
    static void on_connect_callback(struct mosquitto* mosq, void* obj, int rc);
    static void on_disconnect_callback(struct mosquitto* mosq, void* obj, int rc);
    static void on_message_callback(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message);
};

#endif // MQTT_CLIENT_H