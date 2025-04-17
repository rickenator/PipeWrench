#include "SauronWindow.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

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
        auto hb = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
        hb->pack_start(*lbl_host, false, false);
        hb->pack_start(mqtt_host_entry_, true, true);
        mqtt_box_.pack_start(*hb, false, false);
    }
    {
        auto lbl_port = Gtk::manage(new Gtk::Label("Port:"));
        mqtt_port_entry_.set_text("1883");
        auto hb = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
        hb->pack_start(*lbl_port, false, false);
        hb->pack_start(mqtt_port_entry_, true, true);
        mqtt_box_.pack_start(*hb, false, false);
    }
    {
        auto lbl_topic = Gtk::manage(new Gtk::Label("Topic:"));
        mqtt_topic_entry_.set_text("sauron/captures/image");
        auto hb = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
        hb->pack_start(*lbl_topic, false, false);
        hb->pack_start(mqtt_topic_entry_, true, true);
        mqtt_box_.pack_start(*hb, false, false);
    }

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

    // Debug output setup
    debug_streambuf_ = new DebugStreambuf(this);
    cout_buffer_ = std::cout.rdbuf(debug_streambuf_);

    // Initial setup
    ensure_captures_directory();
    refresh_captures();
}

SauronWindow::~SauronWindow() {
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
bool SauronWindow::on_key_press_event(GdkEventKey* key_event) { return false; }
bool SauronWindow::on_delete_event(GdkEventAny* event) { return false; }

void SauronWindow::on_mqtt_connect_clicked() {
    const auto host = mqtt_host_entry_.get_text();
    int port = 1883;
    try {
        port = std::stoi(mqtt_port_entry_.get_text());
    } catch (...) {
        status_bar_.push("Invalid port number");
        return;
    }
    if (!mqtt_connected_) {
        if (mqtt_client_->connect("PipeWrenchClient_" + std::to_string(std::time(nullptr)), host, port)) {
            mqtt_connected_ = true;
            mqtt_connect_button_.set_label("Disconnect");
            mqtt_status_label_.set_markup("<span foreground='green'>Connected</span>");
            status_bar_.push("Connected to MQTT broker at " + host + ":" + std::to_string(port));
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
    if (mqtt_connected_) {
        if (mqtt_client_->publish_image(mqtt_topic_entry_.get_text(), 
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