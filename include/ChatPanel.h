#ifndef CHAT_PANEL_H
#define CHAT_PANEL_H

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/frame.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/cssprovider.h>
#include <memory>
#include <vector>
#include <string>
#include "../include/MqttClient.h"
#include <nlohmann/json.hpp> // Forward declare or include if needed

/**
 * ChatPanel provides a user interface for interacting with the SauronAgent
 * through a chat-like interface.
 */
class ChatPanel : public Gtk::Box {
public:
    ChatPanel(std::shared_ptr<MqttClient> mqtt_client);
    virtual ~ChatPanel();
    
    // Set a function to call when a screen capture is requested
    void set_capture_callback(std::function<std::string()> callback);
    
    // Add a message from captured file
    void add_capture_message(const std::string& filepath);
    
protected:
    // Signal handlers
    void on_new_conversation_clicked();
    void on_save_conversation_clicked();
    void on_load_conversation_clicked();
    bool on_key_press_event(GdkEventKey* event);
    void on_mqtt_message(const std::string& topic, const std::string& payload);
    
private:
    // Message representation
    struct ChatMessage {
        enum class Source {
            USER,
            ASSISTANT,
            SYSTEM
        };
        
        Source source;
        std::string text;
        std::string timestamp;
        std::string image_path; // Optional
        
        // Helper to get CSS class for styling
        std::string get_css_class() const;
    };
    
    // MQTT Client
    std::shared_ptr<MqttClient> mqtt_client_;

    // State
    int active_conversation_id_ = -1; // ID of the currently loaded conversation

    // UI Callbacks
    std::function<std::string()> capture_callback_;

    // Helper methods
    void setup_ui();
    void add_message_to_ui(const ChatMessage& message);
    void clear_messages();
    std::string format_timestamp();
    void load_conversation_list_dialog(const nlohmann::json& conversations_json);
    bool is_connected_to_agent();
    
    // Message handling functions
    void send_message();
    void add_user_message(const std::string& text, const std::string& image_path = "");
    void add_assistant_message(const std::string& text);
    void add_system_message(const std::string& text);

    // UI components
    Gtk::Frame chat_frame_;
    Gtk::Box main_box_;
    
    // Chat history display
    Gtk::ScrolledWindow messages_scrolled_window_;
    Gtk::Box messages_box_;
    
    // Message input area
    Gtk::Frame input_frame_;
    Gtk::Box input_box_;
    Gtk::TextView input_text_view_;
    Glib::RefPtr<Gtk::TextBuffer> input_buffer_;
    
    // Conversation management
    Gtk::Frame conversation_frame_;
    Gtk::Box conversation_box_;
    Gtk::ComboBoxText conversation_combo_;
    Gtk::Button new_conversation_button_;
    Gtk::Button save_conversation_button_;
    Gtk::Button load_conversation_button_;
    
    // Other members
    Glib::RefPtr<Gtk::CssProvider> css_provider_;
    Gtk::Label status_label_;
    std::string selected_image_path_;
};

#endif // CHAT_PANEL_H
