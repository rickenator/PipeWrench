#include "../../include/SauronWindow.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <glibmm/keyfile.h>  // for settings persistence
#include <nlohmann/json.hpp> // Add include for JSON parsing

SauronWindow::DebugStreambuf::DebugStreambuf(SauronWindow* window)
    : window_(window) {}

SauronWindow::DebugStreambuf::int_type SauronWindow::DebugStreambuf::overflow(int_type c) {
    if (c == traits_type::eof()) return traits_type::not_eof(c);
    char ch = traits_type::to_char_type(c);
    buffer_.push_back(ch);
    if (ch == '\n') {
        window_->add_debug_text(buffer_);
        buffer_.clear();
    }
    return traits_type::not_eof(c);
}

SauronWindow::SauronWindow()
    : capturer_(std::make_shared<X11ScreenCapturer>()),
      mqtt_client_(std::make_shared<MqttClient>()),
      sauron_eye_panel_(capturer_, mqtt_client_),
      chat_panel_(mqtt_client_),
      mqtt_connect_button_("Connect"),
      send_button_("Send Latest"),
      debug_buffer_(Gtk::TextBuffer::create())
{
    set_title("Sauron's Eye");
    set_default_size(1024, 768);
    set_border_width(10);

    // Main layout setup
    main_box_.pack_start(content_box_, true, true);
    content_box_.pack_start(left_panel_, true, true);
    content_box_.pack_start(right_panel_, false, false);

    // Left panel setup - Window capture
    left_panel_.set_size_request(600, -1);
    left_panel_.pack_start(sauron_eye_panel_, true, true);
    send_button_.set_sensitive(false); // Initially disabled until we have a capture
    send_button_.signal_clicked().connect(sigc::mem_fun(*this, &SauronWindow::on_send_clicked));
    left_panel_.pack_start(send_button_, false, false);

    // Right panel setup - MQTT Settings
    right_panel_.set_size_request(300, -1);
    mqtt_frame_.add(mqtt_box_);
    mqtt_box_.set_margin_top(10);
    mqtt_box_.set_margin_bottom(10);
    mqtt_box_.set_margin_start(10);
    mqtt_box_.set_margin_end(10);

    // MQTT Settings fields
    {
        auto lbl_host = Gtk::manage(new Gtk::Label("Host:"));
        mqtt_host_entry_.set_text("localhost");
        mqtt_host_entry_.set_editable(true);
        mqtt_host_entry_.set_sensitive(true);
        auto hb = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
        hb->pack_start(*lbl_host, false, false);
        hb->pack_start(mqtt_host_entry_, true, true);
        mqtt_box_.pack_start(*hb, false, false);
    }
    {
        auto lbl_port = Gtk::manage(new Gtk::Label("Port:"));
        mqtt_port_entry_.set_text("1883");
        mqtt_port_entry_.set_editable(true);
        mqtt_port_entry_.set_sensitive(true);
        auto hb = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
        hb->pack_start(*lbl_port, false, false);
        hb->pack_start(mqtt_port_entry_, true, true);
        mqtt_box_.pack_start(*hb, false, false);
    }
    {
        auto lbl_topic = Gtk::manage(new Gtk::Label("Topic:"));
        mqtt_topic_entry_.set_text("sauron");
        mqtt_topic_entry_.set_tooltip_text("Unified MQTT topic for all communication");
        mqtt_topic_entry_.set_editable(false); // Make it non-editable
        mqtt_topic_entry_.set_sensitive(true);
        auto hb = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
        hb->pack_start(*lbl_topic, false, false);
        hb->pack_start(mqtt_topic_entry_, true, true);
        mqtt_box_.pack_start(*hb, false, false);
    }
    // Removed separate command topic entry - using unified topic

    mqtt_status_label_.set_markup("<i>Not connected</i>");
    mqtt_box_.pack_start(mqtt_status_label_, false, false);
    mqtt_connect_button_.signal_clicked().connect(sigc::mem_fun(*this, &SauronWindow::on_mqtt_connect_clicked));
    mqtt_box_.pack_start(mqtt_connect_button_, false, false);
    right_panel_.pack_start(mqtt_frame_, false, false);

    // Recent Captures Panel
    captures_frame_.add(captures_box_);
    captures_scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    captures_scroll_.add(captures_flow_);
    captures_flow_.set_valign(Gtk::ALIGN_START);
    captures_flow_.set_max_children_per_line(2);
    captures_flow_.set_selection_mode(Gtk::SELECTION_SINGLE);
    captures_flow_.set_homogeneous(true);
    captures_flow_.set_column_spacing(5);
    captures_flow_.set_row_spacing(5);
    captures_box_.pack_start(captures_scroll_, true, true);
    open_folder_button_.signal_clicked().connect(sigc::mem_fun(*this, &SauronWindow::on_open_folder_clicked));
    captures_box_.pack_start(open_folder_button_, false, false);
    right_panel_.pack_start(captures_frame_, true, true);
    
    // Chat AI panel setup
    chat_ai_frame_.add(chat_panel_);
    
    right_panel_.pack_start(chat_ai_frame_, true, true);

    // Debug view
    debug_view_.set_buffer(debug_buffer_);
    debug_window_.add(debug_view_);
    debug_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    debug_window_.set_min_content_height(100);
    main_box_.pack_start(debug_window_, true, false);

    // Status bar at the very bottom
    main_box_.pack_start(status_bar_, false, false);

    add(main_box_);
    show_all_children();

    // Connect signals
    sauron_eye_panel_.signal_capture_taken().connect(sigc::mem_fun(*this, &SauronWindow::on_capture_taken));
    // Handle captures from panel by publishing automatically via MQTT Settings
    sauron_eye_panel_.signal_capture_taken_extended().connect(
        sigc::mem_fun(*this, &SauronWindow::on_panel_capture));
        
    // Initialize keyboard shortcuts
    keyboard_controller_.signal_capture_key_pressed().connect(
        sigc::mem_fun(*this, &SauronWindow::on_keyboard_capture_triggered));
    if (keyboard_controller_.start_monitoring()) {
        std::cout << "🔑 Keyboard shortcuts enabled (Numpad Enter to capture)" << std::endl;
    } else {
        std::cout << "⚠️ Keyboard shortcuts could not be enabled" << std::endl;
    }

    // Debug output setup
    debug_streambuf_ = new DebugStreambuf(this);
    cout_buffer_ = std::cout.rdbuf(debug_streambuf_);

    // Load persisted settings if any
    load_settings();

    // Automatically connect to MQTT on startup
    Glib::signal_idle().connect_once([this]() {
        std::cout << "🔌 Auto-connecting to MQTT..." << std::endl;
        on_mqtt_connect_clicked();
    });

    // Initial setup
    ensure_captures_directory();
    refresh_captures();
}

SauronWindow::~SauronWindow() {
    // Stop keyboard monitoring before cleaning up
    keyboard_controller_.stop_monitoring();
    
    // Restore original streams
    std::cout.rdbuf(cout_buffer_);
    delete debug_streambuf_;
}

void SauronWindow::add_debug_text(const std::string& text) {
    Gtk::TextBuffer::iterator start, end;
    debug_buffer_->get_bounds(start, end);
    debug_buffer_->insert(end, text);
    
    // Scroll to end - do this after a brief delay to ensure proper scrolling
    Glib::signal_timeout().connect_once([this]() {
        Gtk::TextBuffer::iterator start, end;
        debug_buffer_->get_bounds(start, end);
        auto mark = debug_buffer_->create_mark(end);
        debug_view_.scroll_to(mark);
        debug_buffer_->delete_mark(mark);
    }, 10);
}

bool SauronWindow::ensure_captures_directory() {
    return true; // handled in Main.cpp
}

// Handle captures coming from SauronEyePanel
void SauronWindow::on_capture_taken(const std::string& filename) {
    last_capture_path_ = filename;
    refresh_captures();
    status_bar_.push("Captured: " + filename);
    send_button_.set_sensitive(true); // Enable send button when a capture is taken
}

// Handle key press and delete events
bool SauronWindow::on_key_press_event(GdkEventKey* key_event) { 
    return Gtk::Window::on_key_press_event(key_event);
}

bool SauronWindow::on_delete_event(GdkEventAny* event) {
    // Check for changes in MQTT settings
    if (mqtt_host_entry_.get_text() != orig_mqtt_host_ ||
        mqtt_port_entry_.get_text() != orig_mqtt_port_) {
        Gtk::MessageDialog dlg(*this, "MQTT Settings: Save changes?", false,
                               Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE);
        dlg.add_button("OK", Gtk::RESPONSE_OK);
        dlg.add_button("Quit", Gtk::RESPONSE_CANCEL);
        int response = dlg.run();
        if (response == Gtk::RESPONSE_OK) {
            save_settings();
        }
    }
    // Proceed with default destroy
    return Gtk::Window::on_delete_event(event);
}

void SauronWindow::on_mqtt_connect_clicked() {
    const auto host = mqtt_host_entry_.get_text();
    int port = 1883;
    try {
        port = std::stoi(mqtt_port_entry_.get_text());
    } catch (...) {
        status_bar_.push("Invalid port number");
        return;
    }
    
    std::cout << "Attempting to connect to MQTT broker at " << host << ":" << port << std::endl;

    if (!mqtt_connected_) {
        if (mqtt_client_->connect(host, "PipeWrenchClient_" + std::to_string(std::time(nullptr)), port)) {
            mqtt_connected_ = true;
            mqtt_connect_button_.set_label("Disconnect");
            mqtt_status_label_.set_markup("<span foreground='green'>Connected</span>");
            status_bar_.push("Connected to MQTT broker at " + host + ":" + std::to_string(port));
            
            // Subscribe to unified topic and set message handler
            const std::string unified_topic = "sauron";
            mqtt_client_->set_message_callback(
                sigc::mem_fun(*this, &SauronWindow::on_mqtt_message));
            if (mqtt_client_->subscribe(unified_topic)) {
                status_bar_.push("Subscribed to topic: " + unified_topic);
            }
        } else {
            status_bar_.push("Failed to connect to MQTT broker");
            mqtt_status_label_.set_markup("<span foreground='red'>Connection failed</span>");
        }
    } else {
        mqtt_client_->disconnect();
        mqtt_connected_ = false;
        mqtt_connect_button_.set_label("Connect");
        mqtt_status_label_.set_markup("<i>Not connected</i>");
        status_bar_.push("Disconnected from MQTT broker");
    }
}

void SauronWindow::on_mqtt_message(const std::string& topic, const std::string& payload) {
    // Handle messages in the UI thread
    Glib::signal_idle().connect_once([this, topic, payload]() {
        if (topic == "sauron") {
            // Check if this is a command message
            try {
                auto j = nlohmann::json::parse(payload);
                if (j.contains("type") && j["type"] == "capture_command" && 
                    j.contains("to") && j["to"] == "ui") {
                    handle_capture_command();
                }
            } catch (const std::exception& e) {
                std::cerr << "Error parsing message: " << e.what() << std::endl;
            }
        }
    });
}

void SauronWindow::handle_capture_command() {
    std::cout << "📸 Received capture command via MQTT" << std::endl;
    sauron_eye_panel_.trigger_capture();
    
    // After capture is taken, the on_capture_taken handler will be called
    // and it will update last_capture_path_. We'll send the image in that handler.
}

void SauronWindow::refresh_captures() {
    // Clear existing thumbnails
    auto children = captures_flow_.get_children();
    for (auto child : children) {
        captures_flow_.remove(*child);
    }

    // Get all PNG files in captures directory and sort by modification time
    const std::string captures_dir = "captures";
    std::vector<std::filesystem::directory_entry> entries;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(captures_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".png") {
                entries.push_back(entry);
            }
        }
        
        // Sort entries by last write time, most recent first
        std::sort(entries.begin(), entries.end(), 
                 [](const auto& a, const auto& b) {
                     return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
                 });

        // Add thumbnails in sorted order
        for (const auto& entry : entries) {
            add_thumbnail(entry.path().string());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading captures directory: " << e.what() << std::endl;
    }
}

void SauronWindow::add_thumbnail(const std::string& filepath) {
    try {
        // Create thumbnail image
        auto pixbuf = Gdk::Pixbuf::create_from_file(filepath, 120, 120, true);
        auto image = Gtk::manage(new Gtk::Image(pixbuf));

        // Create button with thumbnail
        auto button = Gtk::manage(new Gtk::Button());
        button->add(*image);
        button->set_tooltip_text(std::filesystem::path(filepath).filename().string());
        button->signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &SauronWindow::on_thumbnail_clicked), filepath));

        button->show_all();
        captures_flow_.add(*button);

    } catch (const Glib::Exception& e) {
        std::cerr << "Failed to create thumbnail for " << filepath << ": " << e.what() << std::endl;
    }
}

void SauronWindow::on_thumbnail_clicked(const std::string& filepath) {
    const auto topic = mqtt_topic_entry_.get_text();
    std::cout << "Publishing thumbnail to topic: " << topic << std::endl;
    if (mqtt_connected_) {
        if (mqtt_client_->publish_image(topic, 
                                      filepath, "", "manual", true)) {
            status_bar_.push("Sent to MQTT: " + filepath);
        } else {
            status_bar_.push("Failed to send to MQTT: " + filepath);
        }
    }
}

void SauronWindow::on_open_folder_clicked() {
    std::string command;
#ifdef __linux__
    command = "xdg-open captures &";
#elif __APPLE__
    command = "open captures &";
#elif _WIN32
    command = "explorer captures";
#else
    std::cerr << "Unknown platform, cannot open folder" << std::endl;
    return;
#endif
    std::system(command.c_str());
}

void SauronWindow::on_thumbnail_activated_capture(const std::string& filepath) {
    // Open image using system default application
    std::string command;
#ifdef __linux__
    command = "xdg-open \"" + filepath + "\" &";
#elif __APPLE__
    command = "open \"" + filepath + "\" &";
#elif _WIN32
    command = "start \"\" \"" + filepath + "\"";
#else
    std::cerr << "Unknown platform, cannot open file" << std::endl;
    return;
#endif
    
    std::system(command.c_str());
}

void SauronWindow::on_send_clicked() {
    if (!last_capture_path_.empty() && mqtt_connected_) {
        if (mqtt_client_->publish_image(mqtt_topic_entry_.get_text(), 
                                      last_capture_path_, "", "manual", true)) {
            status_bar_.push("Sent latest capture to MQTT: " + last_capture_path_);
        } else {
            status_bar_.push("Failed to send latest capture to MQTT: " + last_capture_path_);
        }
    }
}

void SauronWindow::on_panel_capture(const std::string& filepath, const std::string& type, const std::string& id) {
    if (mqtt_connected_ && !filepath.empty()) {
        const auto topic = mqtt_topic_entry_.get_text();
        // Always encode as Base64
        if (mqtt_client_->publish_image(topic, filepath, "", type, true)) {
            status_bar_.push("Sent capture to MQTT: " + filepath);
        } else {
            status_bar_.push("Failed to send capture to MQTT: " + filepath);
        }
    }
}

void SauronWindow::load_settings() {
    Glib::KeyFile keyfile;
    const std::string fname = "settings.ini";
    if (Glib::file_test(fname, Glib::FILE_TEST_EXISTS)) {
        try {
            keyfile.load_from_file(fname);
            auto host = keyfile.get_string("MQTT", "host");
            auto port = keyfile.get_integer("MQTT", "port");
            mqtt_host_entry_.set_text(host);
            mqtt_port_entry_.set_text(std::to_string(port));
            
            // Always use "sauron" as the unified topic regardless of what's in the settings file
            mqtt_topic_entry_.set_text("sauron");
        } catch (const Glib::Error& e) {
            std::cerr << "Failed to load settings: " << e.what() << std::endl;
        }
    }
    // Store original values
    orig_mqtt_host_ = mqtt_host_entry_.get_text();
    orig_mqtt_port_ = mqtt_port_entry_.get_text();
    // No need to store topic value as it's fixed
}

void SauronWindow::save_settings() {
    Glib::KeyFile keyfile;
    keyfile.set_string("MQTT", "host", mqtt_host_entry_.get_text());
    keyfile.set_integer("MQTT", "port", std::stoi(mqtt_port_entry_.get_text()));
    keyfile.set_string("MQTT", "topic", "sauron"); // Always save the unified topic
    
    try {
        keyfile.save_to_file("settings.ini");
    } catch (const Glib::Error& e) {
        std::cerr << "Failed to save settings: " << e.what() << std::endl;
    }
    // Update original values
    orig_mqtt_host_ = mqtt_host_entry_.get_text();
    orig_mqtt_port_ = mqtt_port_entry_.get_text();
    // No need to track topic values as they're fixed
}

void SauronWindow::on_keyboard_capture_triggered() {
    // Flash the status bar as visual feedback
    status_bar_.push("⌨️ Capture triggered by keyboard shortcut (Numpad Enter)");
    
    // Use the same trigger mechanism as MQTT commands
    std::cout << "📸 Keyboard shortcut triggered capture" << std::endl;
    sauron_eye_panel_.trigger_capture("keyboard");
}