#include "../include/SauronAgent.h"
#include "../include/AIBackend.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp> // Make sure json is included

using json = nlohmann::json;

// Helper functions for timestamp formatting
std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Message role conversion methods
std::string Message::role_to_string() const {
    switch (role) {
        case Role::USER: return "user";
        case Role::ASSISTANT: return "assistant";
        case Role::SYSTEM: return "system";
        default: return "unknown";
    }
}

Message::Role Message::string_to_role(const std::string& role_str) {
    if (role_str == "user") return Role::USER;
    if (role_str == "assistant") return Role::ASSISTANT;
    if (role_str == "system") return Role::SYSTEM;
    return Role::USER; // Default to user if unknown
}

// SauronAgent implementation
SauronAgent::SauronAgent()
    : mqtt_client_(std::make_shared<MqttClient>()),
      db_(nullptr),
      debug_buffer_(Gtk::TextBuffer::create()),
      mqtt_topic_entry_() // Ensure this is initialized if not already
{
}

SauronAgent::~SauronAgent() {
    // Close database connection
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    
    // Disconnect MQTT
    if (mqtt_connected_) {
        mqtt_client_->disconnect();
    }
}

bool SauronAgent::initialize(int argc, char* argv[]) {
    add_debug_text("Initializing SauronAgent...\n");
    
    // Initialize GTK
    auto app = Gtk::Application::create(argc, argv, "com.sauroneye.agent");
    
    // Setup the UI
    setup_ui();
    
    // Initialize database
    if (!initialize_database()) {
        add_debug_text("❌ Failed to initialize database\n");
        return false;
    }
    
    // Load settings
    load_settings(); // Settings are loaded here
    
    add_debug_text("✅ SauronAgent initialized successfully\n");

    // Attempt automatic connection after initialization and loading settings
    // Use Glib::signal_idle().connect_once to ensure UI is ready before connect attempt
    Glib::signal_idle().connect_once([this]() {
        add_debug_text("Attempting automatic MQTT connection...\n");
        on_mqtt_connect_clicked(); // Reuse the button click logic
    });

    return true;
}

void SauronAgent::run() {
    add_debug_text("Starting SauronAgent...\n");
    main_window_.show_all();
    Gtk::Main::run(main_window_);
}

void SauronAgent::add_debug_text(const std::string& text) {
    Gtk::TextBuffer::iterator end = debug_buffer_->end();
    debug_buffer_->insert(end, text);
    
    // Scroll to the end
    Glib::signal_idle().connect_once([this]() {
        auto mark = debug_buffer_->create_mark(debug_buffer_->end());
        debug_view_.scroll_to(mark);
        debug_buffer_->delete_mark(mark);
    });
    
    // Also log to console
    std::cout << text;
}

void SauronAgent::setup_ui() {
    // Configure main window
    main_window_.set_title("Sauron Agent");
    main_window_.set_default_size(800, 600);
    main_window_.set_border_width(10);
    main_window_.add(main_box_);
    
    // Configure config frame
    config_frame_.set_label(" Configuration ");
    config_frame_.set_label_align(Gtk::ALIGN_START);
    config_frame_.set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    config_frame_.add(config_box_);
    
    config_box_.set_spacing(10);
    config_box_.set_margin_top(10);
    config_box_.set_margin_bottom(10);
    config_box_.set_margin_start(10);
    config_box_.set_margin_end(10);
    
    // AI Backend configuration
    auto backend_label = Gtk::manage(new Gtk::Label("AI Backend Configuration", Gtk::ALIGN_START));
    backend_label->set_markup("<b>AI Backend Configuration</b>");
    config_box_.pack_start(*backend_label, false, false);
    
    // Backend type selector
    auto backend_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto backend_type_label = Gtk::manage(new Gtk::Label("Backend Type:"));
    backend_box->pack_start(*backend_type_label, false, false);
    
    backend_type_combo_.append("openai", "OpenAI API");
    backend_type_combo_.append("ollama", "Ollama (Local)");
    backend_type_combo_.set_active(0);
    backend_type_combo_.signal_changed().connect(
        sigc::mem_fun(*this, &SauronAgent::on_backend_type_changed));
    backend_box->pack_start(backend_type_combo_, true, true);
    config_box_.pack_start(*backend_box, false, false);
    
    // API Key
    auto api_key_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto api_key_label = Gtk::manage(new Gtk::Label("API Key:"));
    api_key_box->pack_start(*api_key_label, false, false);
    api_key_entry_.set_visibility(false); // Hide API key
    api_key_box->pack_start(api_key_entry_, true, true);
    config_box_.pack_start(*api_key_box, false, false);
    
    // API Host
    auto api_host_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto api_host_label = Gtk::manage(new Gtk::Label("API Host:"));
    api_host_box->pack_start(*api_host_label, false, false);
    api_host_entry_.set_text("https://api.openai.com/v1");
    api_host_box->pack_start(api_host_entry_, true, true);
    config_box_.pack_start(*api_host_box, false, false);
    
    // Model name
    auto model_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto model_label = Gtk::manage(new Gtk::Label("Model:"));
    model_box->pack_start(*model_label, false, false);
    model_name_entry_.set_text("gpt-4o");
    model_box->pack_start(model_name_entry_, true, true);
    config_box_.pack_start(*model_box, false, false);
    
    // Separator
    config_box_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL)), false, false);

    // MQTT Configuration
    auto mqtt_label = Gtk::manage(new Gtk::Label("MQTT Configuration", Gtk::ALIGN_START));
    mqtt_label->set_markup("<b>MQTT Configuration</b>");
    config_box_.pack_start(*mqtt_label, false, false);

    // MQTT Host
    auto mqtt_host_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto mqtt_host_label = Gtk::manage(new Gtk::Label("MQTT Host:"));
    mqtt_host_box->pack_start(*mqtt_host_label, false, false);
    mqtt_host_entry_.set_text("localhost");
    mqtt_host_box->pack_start(mqtt_host_entry_, true, true);
    config_box_.pack_start(*mqtt_host_box, false, false);
    
    // MQTT Port
    auto mqtt_port_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto mqtt_port_label = Gtk::manage(new Gtk::Label("MQTT Port:"));
    mqtt_port_box->pack_start(*mqtt_port_label, false, false);
    mqtt_port_entry_.set_text("1883");
    mqtt_port_box->pack_start(mqtt_port_entry_, true, true);
    config_box_.pack_start(*mqtt_port_box, false, false);

    // MQTT Topic (Unified) - Keep one entry for clarity, but it's fixed now
    auto mqtt_topic_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    auto mqtt_topic_label = Gtk::manage(new Gtk::Label("MQTT Topic:"));
    mqtt_topic_box->pack_start(*mqtt_topic_label, false, false);
    mqtt_topic_entry_.set_text("sauron"); // Set to unified topic
    mqtt_topic_entry_.set_editable(false); // Make it non-editable
    mqtt_topic_box->pack_start(mqtt_topic_entry_, true, true);
    config_box_.pack_start(*mqtt_topic_box, false, false);

    // MQTT Status and Connect button
    auto mqtt_status_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    mqtt_status_label_.set_markup("<i>Not connected</i>");
    mqtt_status_box->pack_start(mqtt_status_label_, true, true);
    mqtt_connect_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &SauronAgent::on_mqtt_connect_clicked));
    mqtt_status_box->pack_start(mqtt_connect_button_, false, false);
    config_box_.pack_start(*mqtt_status_box, false, false);
    
    // Save settings button
    save_settings_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &SauronAgent::on_save_settings_clicked));
    config_box_.pack_start(save_settings_button_, false, false);
    
    // Add config frame to main box
    main_box_.pack_start(config_frame_, false, false);
    
    // Debug view
    debug_view_.set_buffer(debug_buffer_);
    debug_view_.set_editable(false);
    debug_window_.add(debug_view_);
    debug_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    
    auto debug_label = Gtk::manage(new Gtk::Label("Debug Log", Gtk::ALIGN_START));
    debug_label->set_markup("<b>Debug Log</b>");
    main_box_.pack_start(*debug_label, false, false);
    main_box_.pack_start(debug_window_, true, true);
}

bool SauronAgent::initialize_database() {
    // Ensure directory exists
    std::filesystem::create_directories("data");
    
    // Open database connection
    int rc = sqlite3_open("data/sauron_agent.db", &db_);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ Cannot open database: " + std::string(sqlite3_errmsg(db_)) + "\n");
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    // Create tables if they don't exist
    const char* create_conversations_sql = 
        "CREATE TABLE IF NOT EXISTS conversations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "title TEXT,"
        "created_at TEXT,"
        "updated_at TEXT"
        ");";
    
    const char* create_messages_sql = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "conversation_id INTEGER,"
        "role TEXT,"
        "content TEXT,"
        "timestamp TEXT,"
        "image_path TEXT,"
        "FOREIGN KEY(conversation_id) REFERENCES conversations(id)"
        ");";
    
    char* error_message = nullptr;
    rc = sqlite3_exec(db_, create_conversations_sql, nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ SQL error: " + std::string(error_message) + "\n");
        sqlite3_free(error_message);
        return false;
    }
    
    rc = sqlite3_exec(db_, create_messages_sql, nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ SQL error: " + std::string(error_message) + "\n");
        sqlite3_free(error_message);
        return false;
    }
    
    add_debug_text("✅ Database initialized successfully\n");
    return true;
}

void SauronAgent::load_settings() {
    Glib::KeyFile keyfile;
    const std::string fname = "agent_settings.ini";
    add_debug_text("Attempting to load settings from " + fname + "\n");
    if (Glib::file_test(fname, Glib::FILE_TEST_EXISTS)) {
        try {
            keyfile.load_from_file(fname);
            add_debug_text("Loaded settings file successfully.\n");
            
            // Load AI backend settings
            if (keyfile.has_group("AI")) {
                add_debug_text("Found AI group in settings.\n"); // Add log
                if (keyfile.has_key("AI", "backend_type")) {
                    std::string backend_type = keyfile.get_string("AI", "backend_type");
                    backend_type_combo_.set_active_id(backend_type);
                    add_debug_text("Loaded AI backend_type: " + backend_type + "\n"); // Add log
                } else {
                    add_debug_text("AI key 'backend_type' not found.\n"); // Add log
                }
                if (keyfile.has_key("AI", "api_key")) {
                    std::string api_key = keyfile.get_string("AI", "api_key");
                    api_key_entry_.set_text(api_key);
                    // Don't log the actual key for security
                    add_debug_text("Loaded AI api_key (hidden).\n"); // Add log
                } else {
                     add_debug_text("AI key 'api_key' not found.\n"); // Add log
                }
                if (keyfile.has_key("AI", "api_host")) {
                    std::string api_host = keyfile.get_string("AI", "api_host");
                    api_host_entry_.set_text(api_host);
                     add_debug_text("Loaded AI api_host: " + api_host + "\n"); // Add log
                } else {
                     add_debug_text("AI key 'api_host' not found.\n"); // Add log
                }
                if (keyfile.has_key("AI", "model")) {
                    std::string model = keyfile.get_string("AI", "model");
                    model_name_entry_.set_text(model);
                     add_debug_text("Loaded AI model: " + model + "\n"); // Add log
                } else {
                     add_debug_text("AI key 'model' not found.\n"); // Add log
                }
            } else {
                 add_debug_text("No AI group found in settings.\n"); // Add log
            }
            
            // Load MQTT settings
            if (keyfile.has_group("MQTT")) {
                add_debug_text("Found MQTT group in settings.\n"); // Add log
                if (keyfile.has_key("MQTT", "host")) {
                    mqtt_host_entry_.set_text(keyfile.get_string("MQTT", "host"));
                    add_debug_text("Loaded MQTT host: " + mqtt_host_entry_.get_text() + "\n"); // Add log
                }
                if (keyfile.has_key("MQTT", "port")) {
                    mqtt_port_entry_.set_text(keyfile.get_string("MQTT", "port"));
                     add_debug_text("Loaded MQTT port: " + mqtt_port_entry_.get_text() + "\n"); // Add log
                }
                // Topic is fixed, no need to load
            } else {
                 add_debug_text("No MQTT group found in settings.\n"); // Add log
            }

            add_debug_text("✅ Settings loaded from agent_settings.ini\n");
        } catch (const Glib::Error& e) {
            add_debug_text("❌ Error loading settings file: " + std::string(e.what()) + "\n");
        }
    } else {
        add_debug_text("⚠️ Settings file not found: " + fname + ". Using defaults.\n");
    }
    // DO NOT call on_backend_type_changed() here, as it overwrites loaded settings.
    // The UI should now reflect the values loaded directly from the file.
}

void SauronAgent::save_settings() {
    Glib::KeyFile keyfile;
    
    // Save AI backend settings
    keyfile.set_string("AI", "backend_type", backend_type_combo_.get_active_id());
    keyfile.set_string("AI", "api_key", api_key_entry_.get_text());
    keyfile.set_string("AI", "api_host", api_host_entry_.get_text());
    keyfile.set_string("AI", "model", model_name_entry_.get_text());
    
    // Save MQTT settings (Host and Port only)
    keyfile.set_string("MQTT", "host", mqtt_host_entry_.get_text());
    try {
        keyfile.set_integer("MQTT", "port", std::stoi(mqtt_port_entry_.get_text()));
    } catch (const std::invalid_argument& e) {
        add_debug_text("⚠️ Invalid port number: " + mqtt_port_entry_.get_text() + "\n");
        // Optionally set a default port or handle the error differently
    } catch (const std::out_of_range& e) {
         add_debug_text("⚠️ Port number out of range: " + mqtt_port_entry_.get_text() + "\n");
    }

    // Write to file
    const std::string fname = "agent_settings.ini";
    try {
        keyfile.save_to_file(fname);
        add_debug_text("✅ Settings saved to " + fname + "\n");
    } catch (const Glib::Error& e) {
        add_debug_text("❌ Failed to save settings: " + std::string(e.what()) + "\n");
    }
}

void SauronAgent::on_save_settings_clicked() {
    save_settings();
    
    // Re-initialize the AI backend with new settings
    if (ai_backend_) {
        add_debug_text("🔄 Reinitializing AI backend with new settings...\n");
        initialize_ai_backend();
    }
}

void SauronAgent::on_backend_type_changed() {
    std::string backend_type = backend_type_combo_.get_active_id();
    
    if (backend_type == "openai") {
        api_host_entry_.set_text("https://api.openai.com/v1");
        model_name_entry_.set_text("gpt-4o");
        api_key_entry_.set_sensitive(true);
    } else if (backend_type == "ollama") {
        api_host_entry_.set_text("http://localhost:11434");
        model_name_entry_.set_text("llama3");
        api_key_entry_.set_sensitive(false); // No API key needed for local Ollama
    }
}

void SauronAgent::on_mqtt_connect_clicked() {
    std::string host = mqtt_host_entry_.get_text();
    int port = 1883; // Default port
    try {
        port = std::stoi(mqtt_port_entry_.get_text());
    } catch (...) {
        add_debug_text("⚠️ Invalid port number, using default 1883\n");
    }
    
    add_debug_text("Attempting to connect to MQTT broker at " + host + ":" + std::to_string(port) + "\n");
    
    if (!mqtt_connected_) {
        // Ensure mqtt_client_ is initialized
        if (!mqtt_client_) {
             mqtt_client_ = std::make_shared<MqttClient>(); // Or however it's initialized
        }

        if (mqtt_client_->connect(host, "SauronAgent_" + std::to_string(std::time(nullptr)), port)) {
            mqtt_connected_ = true;
            mqtt_connect_button_.set_label("Disconnect");
            mqtt_status_label_.set_markup("<span foreground='green'>Connected</span>");
            add_debug_text("✅ Connected to MQTT broker\n");
            
            // Subscribe to the unified topic
            const std::string unified_topic = "sauron";
            mqtt_client_->set_message_callback(
                sigc::mem_fun(*this, &SauronAgent::on_mqtt_message));
            if (mqtt_client_->subscribe(unified_topic)) {
                add_debug_text("✅ Subscribed to unified topic: " + unified_topic + "\n");
            } else {
                add_debug_text("❌ Failed to subscribe to unified topic\n");
            }
        } else {
            add_debug_text("❌ Failed to connect to MQTT broker\n");
            mqtt_status_label_.set_markup("<span foreground='red'>Connection failed</span>");
        }
    } else {
        // Disconnect logic
        if (mqtt_client_) {
            mqtt_client_->disconnect();
        }
        mqtt_connected_ = false;
        mqtt_connect_button_.set_label("Connect");
        mqtt_status_label_.set_markup("<span foreground='red'>Disconnected</span>");
        add_debug_text("🔌 Disconnected from MQTT broker\n");
    }
}

void SauronAgent::on_mqtt_message(const std::string& topic, const std::string& payload) {
    add_debug_text("📥 Received message on topic '" + topic + "'\n"); // Payload logged below if valid

    // 1. Check if the topic is the unified "sauron" topic
    if (topic != "sauron") {
        add_debug_text("   Ignoring message on non-sauron topic.\n");
        return;
    }

    try {
        json msg_json = json::parse(payload);

        // 2. Check if the message is intended for the agent ("to": "agent")
        if (!msg_json.contains("to") || !msg_json["to"].is_string() || msg_json["to"] != "agent") {
            // add_debug_text("   Ignoring message not addressed to agent: " + payload + "\n"); // Optional: Log ignored messages
            return;
        }

        // 3. Check if the message is from the UI ("from": "ui") - optional but good practice
        if (!msg_json.contains("from") || !msg_json["from"].is_string() || msg_json["from"] != "ui") {
             add_debug_text("   Warning: Received message for agent but not from UI: " + payload + "\n");
             // Decide whether to process or ignore these
             // return; // Uncomment to strictly ignore non-UI messages
        }


        add_debug_text("   Processing message: " + payload + "\n");
        // Handle incoming messages from UI based on type
        handle_ui_message(msg_json); // Pass parsed JSON

    } catch (const json::parse_error& e) {
        add_debug_text("❌ Error parsing incoming JSON: " + std::string(e.what()) + "\nPayload: " + payload + "\n");
    } catch (const json::type_error& e) {
        add_debug_text("❌ Error accessing JSON fields: " + std::string(e.what()) + "\nPayload: " + payload + "\n");
    } catch (const std::exception& e) {
        add_debug_text("❌ Error processing message: " + std::string(e.what()) + "\n");
    }
}

// Modified handle_ui_message to accept parsed JSON and add routing info to responses
void SauronAgent::handle_ui_message(const json& msg_json) {
    if (!msg_json.contains("type") || !msg_json["type"].is_string()) {
        add_debug_text("❌ Received message without valid 'type' field.\n");
        return;
    }

    std::string type = msg_json["type"];
    const std::string unified_topic = "sauron"; // Use unified topic for all publishes

    try {
        if (type == "user_message") {
            if (!msg_json.contains("message") || !msg_json["message"].is_string()) {
                 add_debug_text("❌ 'user_message' missing 'message' field.\n");
                 return;
            }
            std::string message = msg_json["message"];
            std::string image_path = msg_json.value("image_path", ""); // Use .value for optional fields
            add_debug_text("👤 User message: " + message + (image_path.empty() ? "" : " (with image)") + "\n");
            send_message_to_ai(message, image_path);
        } else if (type == "start_conversation") {
            // Start a new conversation
            Conversation conv;
            conv.title = msg_json.value("title", "New Conversation"); // Use title from message if provided
            conv.created_at = get_current_timestamp();
            conv.updated_at = conv.created_at;
            save_conversation(conv); // This should set conv.id
            active_conversation_id_ = conv.id;

            // Add system message if provided
            if (msg_json.contains("system_message") && msg_json["system_message"].is_string()) {
                Message system_msg;
                system_msg.conversation_id = active_conversation_id_;
                system_msg.role = Message::Role::SYSTEM;
                system_msg.content = msg_json["system_message"];
                system_msg.timestamp = get_current_timestamp();
                save_message(system_msg);
            }

            add_debug_text("🔄 Started new conversation with ID " + std::to_string(active_conversation_id_) + "\n");

            // Notify UI of new conversation creation
            json response;
            response["to"] = "ui";
            response["from"] = "agent";
            response["type"] = "conversation_created";
            response["conversation_id"] = active_conversation_id_;
            response["title"] = conv.title; // Send back the actual title

            if (!mqtt_client_->publish(unified_topic, response.dump())) {
                 add_debug_text("❌ Failed to publish conversation_created response.\n");
            } else {
                 add_debug_text("   📤 Sent conversation_created response.\n");
            }
        } else if (type == "load_conversation") {
            if (!msg_json.contains("conversation_id") || !msg_json["conversation_id"].is_number_integer()) {
                 add_debug_text("❌ 'load_conversation' missing valid 'conversation_id'.\n");
                 return;
            }
            int conversation_id = msg_json["conversation_id"];
            active_conversation_id_ = conversation_id; // Set active conversation
            add_debug_text("   Loading conversation ID: " + std::to_string(conversation_id) + "\n");

            // Load conversation data
            Conversation conv = load_conversation(conversation_id);

            json response;
            response["to"] = "ui";
            response["from"] = "agent";
            response["type"] = "conversation_history";
            response["conversation_id"] = conversation_id;
            response["title"] = conv.title;

            json messages = json::array();
            for (const auto& msg : conv.messages) {
                json message_json;
                message_json["id"] = msg.id;
                message_json["role"] = msg.role_to_string();
                message_json["content"] = msg.content;
                message_json["timestamp"] = msg.timestamp;
                message_json["image_path"] = msg.image_path;
                messages.push_back(message_json);
            }

            response["messages"] = messages; // Add the messages array
            if (!mqtt_client_->publish(unified_topic, response.dump())) {
                 add_debug_text("❌ Failed to publish conversation_history response.\n");
            } else {
                 add_debug_text("   📤 Sent conversation_history response.\n");
            }
        } else if (type == "list_conversations") {
            add_debug_text("   Listing conversations\n");
            std::vector<Conversation> conversations = load_conversations(); // Load all conversations

            json response;
            response["to"] = "ui";
            response["from"] = "agent";
            response["type"] = "conversation_list";

            json conversations_json = json::array();
            for (const auto& conv : conversations) {
                json conv_json;
                conv_json["id"] = conv.id;
                conv_json["title"] = conv.title;
                conv_json["created_at"] = conv.created_at;
                conv_json["updated_at"] = conv.updated_at;

                // Get last message preview (optional, adjust as needed)
                if (!conv.messages.empty()) {
                    const auto& last_msg = conv.messages.back();
                    conv_json["last_message"] = last_msg.content.substr(0, 100); // Preview
                    conv_json["last_message_time"] = last_msg.timestamp;
                }
                conversations_json.push_back(conv_json);
            }

            response["conversations"] = conversations_json;
            if (!mqtt_client_->publish(unified_topic, response.dump())) {
                 add_debug_text("❌ Failed to publish conversation_list response.\n");
            } else {
                 add_debug_text("   📤 Sent conversation_list response.\n");
            }
        } else if (type == "ping") {
            // Respond to ping from UI
            json response;
            response["to"] = "ui";
            response["from"] = "agent";
            response["type"] = "pong";
            if (mqtt_client_->publish(unified_topic, response.dump())) {
                add_debug_text("🏓 Responded to ping from UI\n");
            } else {
                 add_debug_text("❌ Failed to publish pong response.\n");
            }
        } else {
             add_debug_text("❓ Received unknown message type: " + type + "\n");
        }
    } catch (const json::type_error& e) {
        add_debug_text("❌ JSON type error handling message type '" + type + "': " + std::string(e.what()) + "\n");
    } catch (const std::exception& e) {
        add_debug_text("❌ Exception handling message type '" + type + "': " + std::string(e.what()) + "\n");
    }
}

// Modified send_response_to_ui to add routing info and use unified topic
void SauronAgent::send_response_to_ui(const std::string& message_content) {
    if (!mqtt_connected_ || !mqtt_client_) {
        add_debug_text("⚠️ Cannot send response to UI: MQTT not connected\n");
        return;
    }

    json response;
    response["to"] = "ui";
    response["from"] = "agent";

    // Determine if it's an error or assistant message
    // A more robust error handling mechanism might be needed
    if (message_content.rfind("Error:", 0) == 0 || message_content.rfind("❌", 0) == 0) {
         response["type"] = "error";
         response["message"] = message_content;
    } else {
        response["type"] = "assistant_message";
        response["message"] = message_content;
        // Ensure active_conversation_id_ is valid before including it
        if (active_conversation_id_ >= 0) {
             response["conversation_id"] = active_conversation_id_;
        } else {
             add_debug_text("⚠️ Sending assistant message without an active conversation ID.\n");
             // Optionally handle this case, maybe queue the message or force a new conversation?
        }
    }

    const std::string unified_topic = "sauron";
    if (mqtt_client_->publish(unified_topic, response.dump())) {
        add_debug_text("📤 Sent response to UI (Type: " + response["type"].get<std::string>() + ")\n");
    } else {
        add_debug_text("❌ Failed to send response to UI\n");
    }
}

void SauronAgent::send_message_to_ai(const std::string& message, const std::string& image_path) {
    add_debug_text("🤖 Sending message to AI backend...\n");
    
    // Check if backend is initialized
    if (!ai_backend_ || !ai_backend_->is_ready()) {
        initialize_ai_backend();
        
        if (!ai_backend_ || !ai_backend_->is_ready()) {
            add_debug_text("❌ AI backend not initialized\n");
            send_response_to_ui("Error: AI backend not initialized. Please check your configuration.");
            return;
        }
    }
    
    // Save user message to database
    if (active_conversation_id_ < 0) {
        // Create a new conversation if none is active
        Conversation conv;
        conv.title = "New Conversation";
        conv.created_at = get_current_timestamp();
        conv.updated_at = conv.created_at;
        save_conversation(conv);
        active_conversation_id_ = conv.id;
    }
    
    Message user_msg;
    user_msg.conversation_id = active_conversation_id_;
    user_msg.role = Message::Role::USER;
    user_msg.content = message;
    user_msg.timestamp = get_current_timestamp();
    user_msg.image_path = image_path;
    save_message(user_msg);
    
    // Load conversation history for context
    Conversation conv = load_conversation(active_conversation_id_);
    
    // Send message to AI backend
    bool success = ai_backend_->send_message(
        conv.messages, 
        image_path,
        [this](const std::string& response, bool error) {
            if (error) {
                add_debug_text("❌ AI backend error: " + response + "\n");
                send_response_to_ui("Error from AI backend: " + response);
                return;
            }
            
            add_debug_text("✅ Received response from AI backend\n");
            
            // Run on GTK main thread
            Glib::signal_idle().connect_once([this, response]() {
                // Save assistant response to database
                Message assistant_msg;
                assistant_msg.conversation_id = active_conversation_id_;
                assistant_msg.role = Message::Role::ASSISTANT;
                assistant_msg.content = response;
                assistant_msg.timestamp = get_current_timestamp();
                save_message(assistant_msg);
                
                // Send response back to UI
                send_response_to_ui(response);
            });
        }
    );
    
    if (!success) {
        add_debug_text("❌ Failed to send message to AI backend\n");
        send_response_to_ui("Error: Failed to send message to AI backend");
    }
}

bool SauronAgent::save_conversation(Conversation& conversation) {
    if (!db_) {
        add_debug_text("❌ Database not initialized\n");
        return false;
    }
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO conversations (title, created_at, updated_at) VALUES (?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, conversation.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, conversation.created_at.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, conversation.updated_at.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        add_debug_text("❌ Failed to execute statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        sqlite3_finalize(stmt);
        return false;
    }
    
    // Get the last inserted row ID
    conversation.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    
    sqlite3_finalize(stmt);
    return true;
}

bool SauronAgent::save_message(Message& message) {
    if (!db_) {
        add_debug_text("❌ Database not initialized\n");
        return false;
    }
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO messages (conversation_id, role, content, timestamp, image_path) VALUES (?, ?, ?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, message.conversation_id);
    sqlite3_bind_text(stmt, 2, message.role_to_string().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, message.content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, message.timestamp.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, message.image_path.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        add_debug_text("❌ Failed to execute statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        sqlite3_finalize(stmt);
        return false;
    }
    
    // Get the last inserted row ID
    message.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    
    // Update the conversation's updated_at timestamp
    const char* update_sql = "UPDATE conversations SET updated_at = ? WHERE id = ?";
    sqlite3_stmt* update_stmt;
    rc = sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr);
    if (rc == SQLITE_OK) {
        std::string current_time = get_current_timestamp();
        sqlite3_bind_text(update_stmt, 1, current_time.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(update_stmt, 2, message.conversation_id);
        sqlite3_step(update_stmt);
        sqlite3_finalize(update_stmt);
    }
    
    sqlite3_finalize(stmt);
    return true;
}

Conversation SauronAgent::load_conversation(int conversation_id) {
    Conversation conv;
    conv.id = conversation_id;
    
    if (!db_) {
        add_debug_text("❌ Database not initialized\n");
        return conv;
    }
    
    // Load conversation metadata
    sqlite3_stmt* stmt;
    const char* sql = "SELECT title, created_at, updated_at FROM conversations WHERE id = ?";
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        return conv;
    }
    
    sqlite3_bind_int(stmt, 1, conversation_id);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        conv.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        conv.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        conv.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    } else {
        add_debug_text("⚠️ Conversation not found: " + std::to_string(conversation_id) + "\n");
        sqlite3_finalize(stmt);
        return conv;
    }
    
    sqlite3_finalize(stmt);
    
    // Load messages for this conversation
    const char* messages_sql = "SELECT id, role, content, timestamp, image_path FROM messages WHERE conversation_id = ? ORDER BY id";
    
    rc = sqlite3_prepare_v2(db_, messages_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        return conv;
    }
    
    sqlite3_bind_int(stmt, 1, conversation_id);
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Message msg;
        msg.id = sqlite3_column_int(stmt, 0);
        msg.conversation_id = conversation_id;
        
        std::string role_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.role = Message::string_to_role(role_str);
        
        msg.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        msg.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        
        if (sqlite3_column_text(stmt, 4)) {
            msg.image_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        }
        
        conv.messages.push_back(msg);
    }
    
    sqlite3_finalize(stmt);
    return conv;
}

std::vector<Conversation> SauronAgent::load_conversations() {
    std::vector<Conversation> conversations;
    
    if (!db_) {
        add_debug_text("❌ Database not initialized\n");
        return conversations;
    }
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM conversations ORDER BY updated_at DESC";
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        add_debug_text("❌ Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + "\n");
        return conversations;
    }
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int conversation_id = sqlite3_column_int(stmt, 0);
        conversations.push_back(load_conversation(conversation_id));
    }
    
    sqlite3_finalize(stmt);
    return conversations;
}

bool SauronAgent::initialize_ai_backend() {
    // Get backend settings from UI
    std::string backend_type = backend_type_combo_.get_active_id();
    std::string api_key = api_key_entry_.get_text();
    std::string api_host = api_host_entry_.get_text();
    std::string model_name = model_name_entry_.get_text();
    
    // Create appropriate backend
    ai_backend_ = AIBackend::create(backend_type);
    if (!ai_backend_) {
        add_debug_text("❌ Failed to create AI backend\n");
        return false;
    }
    
    // Initialize backend with settings
    add_debug_text("🔄 Initializing " + backend_type + " backend with model: " + model_name + "\n");
    if (!ai_backend_->initialize(api_key, api_host, model_name)) {
        add_debug_text("❌ Failed to initialize AI backend\n");
        return false;
    }
    
    add_debug_text("✅ AI backend initialized successfully\n");
    return true;
}
