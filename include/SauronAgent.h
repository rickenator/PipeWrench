#ifndef SAURON_AGENT_H
#define SAURON_AGENT_H

#include <memory>
#include <string>
#include <vector>
#include <sqlite3.h>
#include <gtkmm.h>
#include <nlohmann/json.hpp>
#include "MqttClient.h"

// Forward declarations
class AIBackend;

/**
 * Represents a message in a conversation
 */
struct Message {
    enum class Role {
        USER,
        ASSISTANT,
        SYSTEM
    };
    
    int id;
    int conversation_id;
    Role role;
    std::string content;
    std::string timestamp;
    std::string image_path; // Optional path to image if message includes one
    
    std::string role_to_string() const;
    static Role string_to_role(const std::string& role_str);
};

/**
 * Represents a conversation with the AI
 */
struct Conversation {
    int id;
    std::string title;
    std::string created_at;
    std::string updated_at;
    std::vector<Message> messages;
};

/**
 * The SauronAgent class manages the communication between SauronEye and AI backends
 */
class SauronAgent {
public:
    SauronAgent();
    ~SauronAgent();
    
    /**
     * Initialize the agent with command line arguments
     */
    bool initialize(int argc, char* argv[]);
    
    /**
     * Run the agent's main loop
     */
    void run();
    
    /**
     * Add a debug message to the log
     */
    void add_debug_text(const std::string& text);
    
private:
    // UI Components
    Gtk::Window main_window_;
    Gtk::Box main_box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Frame config_frame_;
    Gtk::Box config_box_{Gtk::ORIENTATION_VERTICAL};
    
    // AI Backend settings
    Gtk::ComboBoxText backend_type_combo_;
    Gtk::Entry api_key_entry_;
    Gtk::Entry api_host_entry_;
    Gtk::Entry model_name_entry_;
    
    // MQTT settings
    Gtk::Entry mqtt_host_entry_;
    Gtk::Entry mqtt_port_entry_;
    Gtk::Entry mqtt_topic_entry_;
    Gtk::Button mqtt_connect_button_{"Connect"};
    Gtk::Label mqtt_status_label_;
    
    // Debug output
    Gtk::ScrolledWindow debug_window_;
    Gtk::TextView debug_view_;
    Glib::RefPtr<Gtk::TextBuffer> debug_buffer_;
    
    // Control buttons
    Gtk::Button save_settings_button_{"Save Settings"};
    
    // Core components
    std::shared_ptr<MqttClient> mqtt_client_;
    std::shared_ptr<AIBackend> ai_backend_;
    sqlite3* db_;
    
    // State variables
    bool mqtt_connected_{false};
    int active_conversation_id_{-1};
    
    // Handler methods
    void on_backend_type_changed();
    void on_mqtt_connect_clicked();
    void on_mqtt_message(const std::string& topic, const std::string& payload);
    void on_save_settings_clicked();
    
    // Database operations
    bool save_conversation(Conversation& conversation);
    bool save_message(Message& message);
    Conversation load_conversation(int conversation_id);
    std::vector<Conversation> load_conversations();
    
    // AI Backend operations
    bool initialize_ai_backend();
    
    // UI setup
    void setup_ui();
    bool initialize_database();
    void load_settings();
    void save_settings();
    
    // Message handling
    void handle_ui_message(const nlohmann::json& msg_json);
    void send_message_to_ai(const std::string& message, const std::string& image_path);
    void send_response_to_ui(const std::string& message);
};

#endif // SAURON_AGENT_H
