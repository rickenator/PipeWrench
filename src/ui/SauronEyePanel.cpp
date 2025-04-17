#include "../include/SauronEyePanel.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <gtkmm/icontheme.h>

SauronEyePanel::SauronEyePanel(std::shared_ptr<X11ScreenCapturer> capturer, 
                             std::shared_ptr<MqttClient> mqtt_client)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 5),
      screen_capturer_(capturer),
      mqtt_client_(mqtt_client),
      windows_list_store_(Gtk::ListStore::create(windows_columns_))  // Fixed variable name
{
    set_margin_top(10);
    set_margin_bottom(10);
    set_margin_start(10);
    set_margin_end(10);
    
    // Configure capture frame
    capture_frame_.set_label(" Capture ");
    capture_frame_.set_label_align(Gtk::ALIGN_START);
    capture_frame_.set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    
    // Configure capture box
    capture_box_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    capture_box_.set_spacing(10);
    capture_box_.set_margin_top(10);
    capture_box_.set_margin_bottom(10);
    capture_box_.set_margin_start(10);
    capture_box_.set_margin_end(10);
    
    // ===== Windows Section =====
    windows_frame_.set_label(" Windows ");
    windows_frame_.set_label_align(Gtk::ALIGN_START);
    windows_frame_.set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    
    windows_box_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    windows_box_.set_spacing(5);
    windows_box_.set_margin_top(10);
    windows_box_.set_margin_bottom(10);
    windows_box_.set_margin_start(10);
    windows_box_.set_margin_end(10);
    
    // Create list store and tree view for windows
    windows_list_store_ = Gtk::ListStore::create(windows_columns_);
    windows_tree_view_.set_model(windows_list_store_);
    
    // Add columns to tree view
    windows_tree_view_.append_column("", windows_columns_.m_col_icon);
    windows_tree_view_.append_column("Window Title", windows_columns_.m_col_title);
    windows_tree_view_.append_column("Size", windows_columns_.m_col_size);
    windows_tree_view_.signal_row_activated().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_window_activated));
    
    // Configure window list scrolled window
    windows_scrolled_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    windows_scrolled_window_.add(windows_tree_view_);
    windows_scrolled_window_.set_min_content_height(150);
    
    // Configure buttons
    refresh_windows_button_.set_label("Refresh");
    refresh_windows_button_.set_image_from_icon_name("view-refresh");
    refresh_windows_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_refresh_windows_clicked));
    
    capture_window_button_.set_label("Capture Selected");
    capture_window_button_.set_image_from_icon_name("camera-photo");
    capture_window_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_capture_window_clicked));
    
    // Create button box
    Gtk::Box* window_button_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
    window_button_box->set_spacing(5);
    window_button_box->pack_start(refresh_windows_button_, Gtk::PACK_SHRINK);
    window_button_box->pack_end(capture_window_button_, Gtk::PACK_SHRINK);
    
    // Add components to windows box
    windows_box_.pack_start(windows_scrolled_window_, Gtk::PACK_EXPAND_WIDGET);
    windows_box_.pack_start(*window_button_box, Gtk::PACK_SHRINK);
    
    // Add windows box to frame
    windows_frame_.add(windows_box_);
    
    // ===== Screens Section =====
    screens_frame_.set_label(" Screens ");
    screens_frame_.set_label_align(Gtk::ALIGN_START);
    screens_frame_.set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    
    screens_box_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    screens_box_.set_spacing(5);
    screens_box_.set_margin_top(10);
    screens_box_.set_margin_bottom(10);
    screens_box_.set_margin_start(10);
    screens_box_.set_margin_end(10);
    
    // Create list store and tree view for screens
    screens_list_store_ = Gtk::ListStore::create(screens_columns_);
    screens_tree_view_.set_model(screens_list_store_);
    
    // Add columns to tree view
    screens_tree_view_.append_column("", screens_columns_.m_col_icon);
    screens_tree_view_.append_column("Screen", screens_columns_.m_col_name);
    screens_tree_view_.append_column("Resolution", screens_columns_.m_col_resolution);
    
    // Configure screen list scrolled window
    screens_scrolled_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    screens_scrolled_window_.add(screens_tree_view_);
    screens_scrolled_window_.set_min_content_height(100);
    
    // Configure buttons
    refresh_screens_button_.set_label("Refresh");
    refresh_screens_button_.set_image_from_icon_name("view-refresh");
    refresh_screens_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_refresh_screens_clicked));
    
    capture_screen_button_.set_label("Capture Selected");
    capture_screen_button_.set_image_from_icon_name("camera-photo");
    capture_screen_button_.signal_clicked().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_capture_screen_clicked));
    
    // Create button box
    Gtk::Box* screen_button_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
    screen_button_box->set_spacing(5);
    screen_button_box->pack_start(refresh_screens_button_, Gtk::PACK_SHRINK);
    screen_button_box->pack_end(capture_screen_button_, Gtk::PACK_SHRINK);
    
    // Add components to screens box
    screens_box_.pack_start(screens_scrolled_window_, Gtk::PACK_EXPAND_WIDGET);
    screens_box_.pack_start(*screen_button_box, Gtk::PACK_SHRINK);
    
    // Add screens box to frame
    screens_frame_.add(screens_box_);
    
    // ===== Options Section =====
    options_frame_.set_label(" Options ");
    options_frame_.set_label_align(Gtk::ALIGN_START);
    options_frame_.set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    
    options_box_.set_orientation(Gtk::ORIENTATION_VERTICAL);
    options_box_.set_spacing(5);
    options_box_.set_margin_top(10);
    options_box_.set_margin_bottom(10);
    options_box_.set_margin_start(10);
    options_box_.set_margin_end(10);
    
    // Configure delay options
    delay_box_.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
    delay_box_.set_spacing(5);
    
    delay_label_.set_text("Capture delay (seconds):");
    delay_label_.set_halign(Gtk::ALIGN_START);
    
    delay_spin_.set_range(0, 10);
    delay_spin_.set_increments(1, 1);
    delay_spin_.set_value(0);
    
    delay_box_.pack_start(delay_label_, Gtk::PACK_SHRINK);
    delay_box_.pack_start(delay_spin_, Gtk::PACK_SHRINK);
    
    // Add options to box
    options_box_.pack_start(delay_box_, Gtk::PACK_SHRINK);
    
    // Add options box to frame
    options_frame_.add(options_box_);
    
    // Add all sections to main box
    pack_start(windows_frame_, Gtk::PACK_EXPAND_WIDGET);
    pack_start(screens_frame_, Gtk::PACK_EXPAND_WIDGET);
    pack_start(options_frame_, Gtk::PACK_SHRINK);
    
    // Populate initial data
    refresh_window_list();
    refresh_screen_list();
    
    show_all_children();
}

SauronEyePanel::~SauronEyePanel() {
}

void SauronEyePanel::refresh_window_list() {
    windows_list_store_->clear();
    
    auto windows = screen_capturer_->list_windows();
    for (const auto& window_info : windows) {
        Gtk::TreeModel::Row row = *(windows_list_store_->append());
        // Load icon via IconTheme
        auto icon_theme = Gtk::IconTheme::get_default();
        Glib::RefPtr<Gdk::Pixbuf> icon;
        try {
            icon = icon_theme->load_icon("window-new", 24, Gtk::ICON_LOOKUP_USE_BUILTIN);
        } catch (const Glib::Error& ex) {
            try {
                icon = icon_theme->load_icon("preferences-system-windows", 24, Gtk::ICON_LOOKUP_USE_BUILTIN);
            } catch (const Glib::Error& ex2) {
                std::cerr << "⚠️ Window icon not found, using fallback" << std::endl;
                icon = icon_theme->load_icon("image-missing", 24, Gtk::ICON_LOOKUP_USE_BUILTIN);
            }
        }
        
        row[windows_columns_.m_col_icon] = icon;
        row[windows_columns_.m_col_title] = window_info.title;
        row[windows_columns_.m_col_id] = window_info.id;
        
        std::string size_str = std::to_string(window_info.width) + "×" + 
                              std::to_string(window_info.height);
        row[windows_columns_.m_col_size] = size_str;
    }
}

void SauronEyePanel::refresh_screen_list() {
    screens_list_store_->clear();
    
    auto screens = screen_capturer_->detect_screens();
    for (const auto& screen_info : screens) {
        Gtk::TreeModel::Row row = *(screens_list_store_->append());
        // Load icon via IconTheme
        auto icon_theme = Gtk::IconTheme::get_default();
        Glib::RefPtr<Gdk::Pixbuf> icon;
        try {
            icon = icon_theme->load_icon("video-display", 24, Gtk::ICON_LOOKUP_USE_BUILTIN);
        } catch (const Glib::Error& ex) {
            std::cerr << "⚠️ Icon 'video-display' not found: " << ex.what() << ", using fallback" << std::endl;
            icon = icon_theme->load_icon("image-missing", 24, Gtk::ICON_LOOKUP_USE_BUILTIN);
        }
        row[screens_columns_.m_col_icon] = icon;
        row[screens_columns_.m_col_name] = screen_info.name;
        // use screen number as id
        row[screens_columns_.m_col_id] = screen_info.number;
        std::string resolution = std::to_string(screen_info.width) + "×" + std::to_string(screen_info.height);
        row[screens_columns_.m_col_resolution] = resolution;
    }
}

void SauronEyePanel::on_refresh_windows_clicked() {
    refresh_window_list();
}

void SauronEyePanel::on_capture_window_clicked() {
    Glib::RefPtr<Gtk::TreeSelection> selection = windows_tree_view_.get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        unsigned long window_id = row[windows_columns_.m_col_id];
        
        take_capture("window", window_id);
    } else {
        std::cerr << "❌ No window selected" << std::endl;
    }
}

void SauronEyePanel::on_refresh_screens_clicked() {
    refresh_screen_list();
}

void SauronEyePanel::on_capture_screen_clicked() {
    Glib::RefPtr<Gtk::TreeSelection> selection = screens_tree_view_.get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        int screen_id = row[screens_columns_.m_col_id];
        
        // Use the screen_id in place of window_id for screen capture
        take_capture("screen", screen_id);
    } else {
        std::cerr << "❌ No screen selected" << std::endl;
    }
}

void SauronEyePanel::on_window_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*) {
    Gtk::TreeModel::iterator iter = windows_list_store_->get_iter(path);
    if (iter) {
        unsigned long window_id = (*iter)[windows_columns_.m_col_id];
        // Find the window info for this ID
        auto windows = screen_capturer_->list_windows();
        auto it = std::find_if(windows.begin(), windows.end(),
                             [window_id](const X11ScreenCapturer::WindowInfo& w) {
                                 return w.id == window_id;
                             });
        if (it != windows.end()) {
            auto now = std::chrono::system_clock::now();
            std::time_t now_time = std::chrono::system_clock::to_time_t(now);

            // Format filename with timestamp
            std::stringstream ss;
            ss << "captures/window_" << std::put_time(std::localtime(&now_time), "%Y%m%d_%H%M%S") << ".png";
            std::string filename = ss.str();

            // Take the capture
            if (screen_capturer_->capture_window(*it, filename)) {
                m_signal_capture_taken.emit(filename);
                // After successful capture, immediately send via MQTT if connected
                if (mqtt_client_) {
                    mqtt_client_->publish_image("sauron/captures/image", filename, "", "auto", false);
                }
            }
        }
    }
}

std::string SauronEyePanel::take_capture(const std::string& type, unsigned long id) {
    int delay_seconds = delay_spin_.get_value_as_int();
    
    // Handle delay if set
    if (delay_seconds > 0) {
        std::cout << "🕒 Waiting " << delay_seconds << " seconds before capture..." << std::endl;
        
        // Countdown feedback
        for (int i = delay_seconds; i > 0; --i) {
            std::cout << "🕒 " << i << "..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    std::string capture_filename;
    bool success = false;
    std::string timestamp;
    {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S");
        timestamp = oss.str();
    }
    
    try {
        if (type == "window") {
            // find window info
            X11ScreenCapturer::WindowInfo target;
            bool found = false;
            for (auto& w : screen_capturer_->list_windows()) {
                if (w.id == id) { target = w; found = true; break; }
            }
            if (!found) { std::cerr << "❌ Window id not found\n"; return ""; }
            capture_filename = "captures/window_" + timestamp + ".png";
            success = screen_capturer_->capture_window(target, capture_filename);
        } else if (type == "screen") {
            // find screen info
            bool found = false;
            for (auto& s : screen_capturer_->detect_screens()) {
                if ((unsigned long)s.number == id) { found = true; break; }
            }
            if (!found) { std::cerr << "❌ Screen id not found\n"; return ""; }
            capture_filename = "captures/screen_" + timestamp + ".png";
            success = screen_capturer_->capture_screen((int)id, capture_filename);
        } else {
            std::cerr << "❌ Unknown capture type: " << type << std::endl;
            return "";
        }
        
        if (success) {
            std::cout << "✅ Capture saved to: " << capture_filename << std::endl;
            
            // Emit signal with capture filename
            m_signal_capture_taken.emit(capture_filename);
        } else {
            std::cerr << "❌ Failed to capture " << type << std::endl;
        }
    } catch (const std::exception& ex) {
        std::cerr << "❌ Exception during capture: " << ex.what() << std::endl;
    }
    
    return capture_filename;
}