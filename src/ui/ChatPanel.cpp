#include "../include/ChatPanel.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream> // Required for std::ostringstream
#include <iomanip> // Required for std::setw, std::setfill, std::hex
#include <glibmm/main.h>
#include <string> // Required for std::string operations
#include <cctype> // Required for iscntrl
#include <nlohmann/json.hpp> // Include the JSON library

// Helper function to escape strings for JSON - No longer strictly needed for payload creation
// but might be useful elsewhere, or can be removed if unused.
std::string escape_json_string(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"':  ss << "\\\""; break;
            case '/':  ss << "\\/"; break; // Optional, but good practice
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                // Check for control characters (U+0000 to U+001F)
                if (iscntrl(c)) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

ChatPanel::ChatPanel(std::shared_ptr<MqttClient> mqtt_client)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 10), // Base class constructor first
      // Initialize in declaration order from ChatPanel.h
      main_box_(Gtk::ORIENTATION_VERTICAL, 10),
      // messages_scrolled_window_ (default constructor)
      // messages_box_ (default constructor)
      // input_frame_ (default constructor)
      input_box_(Gtk::ORIENTATION_HORIZONTAL, 5),
      // input_text_view_ (default constructor)
      input_buffer_(Gtk::TextBuffer::create()),
      // send_button_("Send"), // Removed
      // attach_button_("Attach Image"), // Removed
      // conversation_frame_ (default constructor)
      conversation_box_(Gtk::ORIENTATION_HORIZONTAL, 5),
      // conversation_combo_ (default constructor)
      new_conversation_button_("New"),
      save_conversation_button_("Save"),
      load_conversation_button_("Load"),
      // status_label_ (default constructor)
      mqtt_client_(mqtt_client) // Initialize mqtt_client_ last as per declaration order
{
    setup_ui();
    
    if (mqtt_client_) {
        mqtt_client_->set_message_callback(
            sigc::mem_fun(*this, &ChatPanel::on_mqtt_message));
        
        // Subscribe to the unified topic
        mqtt_client_->subscribe("sauron"); 
    }
    
    add_system_message("Welcome to SauronEye AI Chat. Type a message to start a conversation.");
}

ChatPanel::~ChatPanel() {
    // Clean up any resources if needed
}

void ChatPanel::setup_ui() {
    // Set up CSS for message styling
    css_provider_ = Gtk::CssProvider::create();
    try {
        // Use updated CSS for better readability
        css_provider_->load_from_data(
            ".user-message { background-color: #e3f2fd; padding: 5px; margin: 3px; border-radius: 5px; }\n" // Light blue background for user
            ".assistant-message { background-color: #f1f1f1; padding: 5px; margin: 3px; border-radius: 5px; }\n" // Light grey background for assistant
            ".system-message { font-style: italic; color: #666; margin: 5px 0; }\n" // Italic grey for system
            ".timestamp { font-size: small; color: #9e9e9e; }"
        );
    } catch (const Gtk::CssProviderError& e) {
        // Print specific CSS error
        std::cerr << "CSS Provider Error during load_from_data: " << e.what() << std::endl;
        // You might get more details depending on the error:
        // std::cerr << "Details: " << e.to_string() << std::endl; // If available
        throw; // Rethrow to see the original crash behavior if needed
    } catch (const Glib::Error& e) {
        // Catch potential Glib errors during CSS loading
        std::cerr << "Glib Error during CSS load_from_data: " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) {
        // Catch any other standard exceptions
        std::cerr << "Standard Exception during CSS load_from_data: " << e.what() << std::endl;
        throw;
    } catch (...) {
        // Catch any other unknown exceptions
        std::cerr << "Unknown Exception during CSS load_from_data." << std::endl;
        throw;
    }

    Glib::RefPtr<Gtk::StyleContext> style_context = get_style_context();
    // Applying provider to the screen. If this line causes the error, the load_from_data was likely okay.
    try {
         style_context->add_provider_for_screen(Gdk::Screen::get_default(), css_provider_, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    } catch (const Glib::Error& e) {
        std::cerr << "Glib Error during add_provider_for_screen: " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) {
        std::cerr << "Standard Exception during add_provider_for_screen: " << e.what() << std::endl;
        throw;
    } catch (...) {
        std::cerr << "Unknown Exception during add_provider_for_screen." << std::endl;
        throw;
    }

    // Configure main layout
    set_border_width(10);
    
    // Set up conversation management frame
    conversation_frame_.set_label(" Conversations ");
    conversation_frame_.add(conversation_box_);
    conversation_box_.set_border_width(10);
    
    conversation_box_.pack_start(conversation_combo_, true, true);
    conversation_box_.pack_start(new_conversation_button_, false, false);
    conversation_box_.pack_start(save_conversation_button_, false, false);
    conversation_box_.pack_start(load_conversation_button_, false, false);
    
    // Set up chat history display
    chat_frame_.set_label(" Chat ");
    messages_box_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    messages_scrolled_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    messages_scrolled_window_.add(messages_box_);
    messages_scrolled_window_.set_min_content_height(300);
    chat_frame_.add(messages_scrolled_window_);
    
    // Set up input area
    input_frame_.set_label(" Message ");
    input_text_view_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    input_text_view_.set_buffer(input_buffer_);
    input_text_view_.set_size_request(-1, 80);
    input_text_view_.signal_key_press_event().connect(
        sigc::mem_fun(*this, &ChatPanel::on_key_press_event), false);
    
    input_box_.pack_start(input_text_view_, true, true);
    input_frame_.add(input_box_);
    
    // Initialize status label (but don't add to UI)
    status_label_.set_markup("<i>Not connected to AI agent</i>");
    status_label_.set_halign(Gtk::ALIGN_START);
    
    // Connect signals
    new_conversation_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatPanel::on_new_conversation_clicked));
    save_conversation_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatPanel::on_save_conversation_clicked));
    load_conversation_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &ChatPanel::on_load_conversation_clicked));
    
    // Add everything to main container
    pack_start(conversation_frame_, false, false);
    pack_start(chat_frame_, true, true);
    pack_start(input_frame_, false, false);
    
    show_all_children();
}

bool ChatPanel::on_key_press_event(GdkEventKey* event) {
    // Send message on Enter (but not Shift+Enter which inserts a new line)
    if (event->keyval == GDK_KEY_Return) {
        // If Shift is pressed, allow default behavior (new line)
        if (event->state & GDK_SHIFT_MASK) {
            return false;
        }
        // If Control is pressed, also send
        // Otherwise (plain Enter), send the message
        send_message();
        return true;
    }
    return false;
}

void ChatPanel::on_new_conversation_clicked() {
    if (!is_connected_to_agent()) {
        add_system_message("Could not connect to AI agent. Please check if SauronAgent is running.");
        return;
    }
    
    // Send request to create a new conversation using nlohmann::json
    nlohmann::json request_json;
    request_json["to"] = "agent";
    request_json["from"] = "ui";
    request_json["type"] = "start_conversation";
    request_json["title"] = "New Conversation"; // No need to manually escape

    std::string request_str = request_json.dump(); // Serialize JSON object to string

    if (mqtt_client_->publish("sauron", request_str)) { // Use unified topic
        add_system_message("Starting new conversation...");
        clear_messages();
    } else {
        add_system_message("Failed to start new conversation.");
    }
}

void ChatPanel::on_save_conversation_clicked() {
    if (active_conversation_id_ < 0) {
        add_system_message("No active conversation to save.");
        return;
    }
    
    // In a real implementation, we would ask for a title here
    add_system_message("Conversation saved.");
}

void ChatPanel::on_load_conversation_clicked() {
    if (!is_connected_to_agent()) {
        add_system_message("Could not connect to AI agent. Please check if SauronAgent is running.");
        return;
    }
    
    // Request list of conversations using nlohmann::json
    nlohmann::json request_json;
    request_json["to"] = "agent";
    request_json["from"] = "ui";
    request_json["type"] = "list_conversations";

    std::string request_str = request_json.dump();

    if (mqtt_client_->publish("sauron", request_str)) { // Use unified topic
        add_system_message("Requesting conversation list..."); // Changed message
    } else {
        add_system_message("Failed to request conversation list."); // Changed message
    }
    // Don't call load_conversation_list() here, wait for the response
}

void ChatPanel::load_conversation_list_dialog(const nlohmann::json& conversations_json) {
    // This is called after receiving the conversation list from the agent

    Gtk::Dialog dialog("Select Conversation", *dynamic_cast<Gtk::Window*>(get_toplevel()), true);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Load", Gtk::RESPONSE_OK);

    auto content_area = dialog.get_content_area();
    Gtk::ComboBoxText combo;

    if (conversations_json.is_array()) {
         for (const auto& conv_json : conversations_json) {
             if (conv_json.contains("id") && conv_json["id"].is_number_integer() &&
                 conv_json.contains("title") && conv_json["title"].is_string()) {
                 int id = conv_json["id"];
                 std::string title = conv_json["title"];
                 combo.append(std::to_string(id), title);
             }
         }
    }

    // Check if the combo box is empty using the model size
    if (!combo.get_model() || combo.get_model()->children().empty()) { // Correct way to check if empty
        add_system_message("No conversations available to load.");
        return;
    }

    combo.set_active(0);
    content_area->pack_start(combo);
    content_area->show_all();

    int result = dialog.run();
    if (result == Gtk::RESPONSE_OK) {
        std::string selected_id_str = combo.get_active_id();
        int selected_id = -1;
        try {
            selected_id = std::stoi(selected_id_str);
        } catch (...) {
             add_system_message("Invalid conversation ID selected.");
             return;
        }

        // Request to load the selected conversation using nlohmann::json
        nlohmann::json request_json;
        request_json["to"] = "agent";
        request_json["from"] = "ui";
        request_json["type"] = "load_conversation";
        request_json["conversation_id"] = selected_id; // Use integer directly

        std::string request_str = request_json.dump();

        if (mqtt_client_->publish("sauron", request_str)) { // Use unified topic
            add_system_message("Loading conversation " + selected_id_str + "...");
            clear_messages(); // Clear messages while waiting for history
        } else {
             add_system_message("Failed to request conversation load.");
        }
    }
}

void ChatPanel::send_message() {
    Glib::ustring ustring_text = input_buffer_->get_text();
    std::string text = ustring_text; // Convert Glib::ustring to std::string

    if (text.empty()) {
        return;  // Don't send empty messages
    }
    
    // Clear input right away to prevent duplicate sends
    input_buffer_->set_text("");

    // Check if we're connected to MQTT before attempting to send
    if (!is_connected_to_agent()) {
        add_system_message("Cannot send message: Not connected to MQTT broker.");
        return;
    }

    // If we have a new conversation
    if (active_conversation_id_ < 0) {
        on_new_conversation_clicked();
        // Consider waiting for conversation_created response before sending the message
        // For simplicity now, we send immediately, but this might race.
    }

    // Add message to UI
    add_user_message(text);

    // Send message to agent via MQTT using nlohmann::json
    nlohmann::json message_json;
    message_json["to"] = "agent";
    message_json["from"] = "ui";
    message_json["type"] = "text"; // Changed from "user_message" to "text" as requested
    message_json["data"] = text; // Use "data" field instead of "message" for text content
    
    // Include conversation ID if available
    if (active_conversation_id_ >= 0) {
         message_json["conversation_id"] = active_conversation_id_;
    }

    std::string message_str = message_json.dump();

    std::cout << "DEBUG: Attempting to send message directly: " << message_str << std::endl;
    if (mqtt_client_->publish("sauron", message_str)) { // Use unified topic
        std::cout << "DEBUG: Successfully published message" << std::endl;
        selected_image_path_ = "";
    } else {
        std::cout << "DEBUG: Failed to publish message" << std::endl;
        add_system_message("Failed to send message to AI agent.");
    }
}

void ChatPanel::on_mqtt_message(const std::string& topic, const std::string& payload) {
    // Only process messages on the unified topic
    if (topic != "sauron") {
        return;
    }
    
    try {
        // Parse the payload using nlohmann::json
        auto json_payload = nlohmann::json::parse(payload);

        // Check if the message is intended for the UI
        if (!json_payload.contains("to") || json_payload["to"] != "ui") {
            // std::cout << "Ignoring message not for UI: " << payload << std::endl; // Optional debug
            return;
        }

        // Check if 'type' field exists
        if (!json_payload.contains("type") || !json_payload["type"].is_string()) { // Check type is string
             std::cerr << "Received message without string 'type' field: " << payload << std::endl;
             return;
        }

        std::string type = json_payload["type"];

        // Helper lambda to safely get string value
        auto get_string = [&](const nlohmann::json& obj, const std::string& key, const std::string& default_val = "") -> std::string {
            return obj.contains(key) && obj[key].is_string() ? obj[key].get<std::string>() : default_val;
        };

        // Helper lambda to safely get int value
        auto get_int = [&](const nlohmann::json& obj, const std::string& key, int default_val = -1) -> int {
            // Allow string numbers too for flexibility from agent
            if (obj.contains(key)) {
                if (obj[key].is_number_integer()) return obj[key].get<int>();
                if (obj[key].is_string()) {
                    try { return std::stoi(obj[key].get<std::string>()); } catch (...) {}
                }
            }
            return default_val;
        };


        if (type == "assistant_message") {
            std::string message = get_string(json_payload, "message");
            int conv_id = get_int(json_payload, "conversation_id");
            if (conv_id >= 0) active_conversation_id_ = conv_id; // Update active ID if provided

            Glib::signal_idle().connect_once([this, message]() {
                add_assistant_message(message);
            });
        } else if (type == "conversation_created") {
            active_conversation_id_ = get_int(json_payload, "conversation_id");
            std::string title = get_string(json_payload, "title");

            Glib::signal_idle().connect_once([this, title]() {
                add_system_message("New conversation started: " + title);
                // Optionally update conversation combo box here if needed
            });
        } else if (type == "conversation_history") {
             active_conversation_id_ = get_int(json_payload, "conversation_id");
             std::string title = get_string(json_payload, "title");

             // Clear existing messages and add history
             // Capture necessary variables by reference [&]
             Glib::signal_idle().connect_once([&, this, title, json_payload]() {
                 clear_messages();
                 add_system_message("Loaded conversation: " + title);
                 if (json_payload.contains("messages") && json_payload["messages"].is_array()) {
                     for (const auto& msg_json : json_payload["messages"]) {
                         std::string msg_text = get_string(msg_json, "content");
                         std::string msg_role = get_string(msg_json, "role");
                         std::string img_path = get_string(msg_json, "image_path");

                         if (msg_role == "user") {
                             add_user_message(msg_text, img_path);
                         } else if (msg_role == "assistant") {
                             add_assistant_message(msg_text);
                         }
                         // Ignore system messages in history for now? Or add them?
                     }
                 }
             });

        } else if (type == "conversation_list") {
             Glib::signal_idle().connect_once([this, json_payload]() {
                 if (json_payload.contains("conversations")) {
                     // Call the function to show the dialog with the received list
                     load_conversation_list_dialog(json_payload["conversations"]);
                 } else {
                      add_system_message("Received empty or invalid conversation list data.");
                 }
             });
        } else if (type == "error") {
            std::string error_message = get_string(json_payload, "message", "Unknown error from agent.");
            Glib::signal_idle().connect_once([this, error_message]() {
                add_system_message("Error from agent: " + error_message);
            });
        } else {
             std::cerr << "Received unknown message type: " << type << std::endl;
             Glib::signal_idle().connect_once([this, type]() {
                 add_system_message("Received unhandled message type from agent: " + type);
             });
        }
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Error parsing incoming JSON (nlohmann): " << e.what() << std::endl;
        std::cerr << "Payload: " << payload << std::endl;
        Glib::signal_idle().connect_once([this]() {
            add_system_message("Error parsing message from agent.");
        });
    } catch (const std::exception& e) {
        std::cerr << "Error processing MQTT message: " << e.what() << std::endl;
         Glib::signal_idle().connect_once([this]() {
            add_system_message("Error processing message from agent.");
        });
    }
}

void ChatPanel::add_user_message(const std::string& text, const std::string& image_path) {
    ChatMessage msg;
    msg.source = ChatMessage::Source::USER;
    msg.text = text;
    msg.timestamp = format_timestamp();
    msg.image_path = image_path;
    add_message_to_ui(msg);
}

void ChatPanel::add_assistant_message(const std::string& text) {
    ChatMessage msg;
    msg.source = ChatMessage::Source::ASSISTANT;
    msg.text = text;
    msg.timestamp = format_timestamp();
    add_message_to_ui(msg);
}

void ChatPanel::add_system_message(const std::string& text) {
    ChatMessage msg;
    msg.source = ChatMessage::Source::SYSTEM;
    msg.text = text;
    msg.timestamp = format_timestamp();
    add_message_to_ui(msg);
}

void ChatPanel::add_message_to_ui(const ChatMessage& message) {
    std::cout << "DEBUG: add_message_to_ui called for source " << static_cast<int>(message.source) << " with text: \"" << message.text << "\"" << std::endl; // DEBUG ADDED
    // Create message container
    auto msg_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
    
    // Set CSS class for styling
    Glib::RefPtr<Gtk::StyleContext> style_context = msg_box->get_style_context();
    style_context->add_class(message.get_css_class());
    
    // If it's a user or assistant message, align it properly
    if (message.source == ChatMessage::Source::USER) {
        msg_box->set_halign(Gtk::ALIGN_END);
    } else if (message.source == ChatMessage::Source::ASSISTANT) {
        msg_box->set_halign(Gtk::ALIGN_START);
    } else {
        msg_box->set_halign(Gtk::ALIGN_CENTER);
    }
    
    // Add text content
    auto text_label = Gtk::manage(new Gtk::Label(message.text));
    text_label->set_line_wrap(true);
    text_label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
    text_label->set_halign(Gtk::ALIGN_START);
    text_label->set_xalign(0.0);
    
    // Use markup for message text if it's a system message
    if (message.source == ChatMessage::Source::SYSTEM) {
        text_label->set_markup("<i>" + message.text + "</i>");
    }
    
    msg_box->pack_start(*text_label, false, false);
    
    // Add image if present
    if (!message.image_path.empty()) {
        try {
            auto image = Gtk::manage(new Gtk::Image());
            auto pixbuf = Gdk::Pixbuf::create_from_file(message.image_path);
            
            // Scale image if it's too large
            int max_width = 400;
            int max_height = 300;
            
            if (pixbuf->get_width() > max_width || pixbuf->get_height() > max_height) {
                double scale = std::min(
                    static_cast<double>(max_width) / pixbuf->get_width(),
                    static_cast<double>(max_height) / pixbuf->get_height()
                );
                int new_width = static_cast<int>(pixbuf->get_width() * scale);
                int new_height = static_cast<int>(pixbuf->get_height() * scale);
                
                pixbuf = pixbuf->scale_simple(new_width, new_height, Gdk::INTERP_BILINEAR);
            }
            
            image->set(pixbuf);
            msg_box->pack_start(*image, false, false);
        } catch (const Glib::Exception& e) {
            auto error_label = Gtk::manage(new Gtk::Label("Failed to load image: " + message.image_path));
            error_label->set_markup("<span foreground='red'><i>Failed to load image</i></span>");
            msg_box->pack_start(*error_label, false, false);
        }
    }
    
    // Add timestamp
    auto timestamp_label = Gtk::manage(new Gtk::Label(message.timestamp));
    timestamp_label->set_markup("<span size='small' foreground='#9e9e9e'>" + message.timestamp + "</span>");
    timestamp_label->set_halign(Gtk::ALIGN_END);
    msg_box->pack_start(*timestamp_label, false, false);
    
    // Add to messages container
    messages_box_.pack_start(*msg_box, false, false);
    
    // Show all new widgets
    messages_box_.show_all();
    
    // Scroll to bottom
    Glib::signal_idle().connect_once([this]() {
        auto adjustment = messages_scrolled_window_.get_vadjustment();
        adjustment->set_value(adjustment->get_upper());
    });
}

void ChatPanel::clear_messages() {
    // Remove all children from messages box
    auto children = messages_box_.get_children();
    for (auto child : children) {
        messages_box_.remove(*child);
        delete child;
    }
}

std::string ChatPanel::format_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%H:%M:%S");
    return ss.str();
}

void ChatPanel::set_capture_callback(std::function<std::string()> callback) {
    capture_callback_ = callback;
}

void ChatPanel::add_capture_message(const std::string& filepath) {
    // Add the capture preview to the UI
    ChatMessage msg;
    msg.source = ChatMessage::Source::USER;
    // Define the message text to be sent with the image
    std::string message_text = "Analyze this screenshot"; 
    msg.text = message_text + " (" + filepath + ")"; // Display text includes path
    msg.timestamp = format_timestamp();
    msg.image_path = filepath;
    add_message_to_ui(msg);

    // If connected to agent, send the image to the AI
    if (is_connected_to_agent()) {

        // Use nlohmann::json to build the message
        nlohmann::json message_json;
        message_json["to"] = "agent";
        message_json["from"] = "ui";
        message_json["type"] = "user_message";
        message_json["message"] = message_text; // Use the defined message text
        message_json["image_path"] = filepath;
         // Include conversation ID if available
        if (active_conversation_id_ >= 0) {
             message_json["conversation_id"] = active_conversation_id_;
        }

        std::string message_str = message_json.dump();

        if (mqtt_client_->publish("sauron", message_str)) { // Use unified topic
            // Optionally add a system message confirming send
            // add_system_message("Capture sent to agent.");
        } else {
            add_system_message("Failed to send capture to AI agent.");
        }
    } else {
         add_system_message("Cannot send capture: Not connected to agent.");
    }
}

bool ChatPanel::is_connected_to_agent() {
    // Only check if MQTT client is connected
    return mqtt_client_ && mqtt_client_->is_connected();
}

std::string ChatPanel::ChatMessage::get_css_class() const {
    switch (source) {
        case Source::USER:
            return "user-message";
        case Source::ASSISTANT:
            return "assistant-message";
        case Source::SYSTEM:
            return "system-message";
        default:
            return "";
    }
}
