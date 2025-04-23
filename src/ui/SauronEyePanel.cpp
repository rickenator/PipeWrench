#include "../include/SauronEyePanel.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <gtkmm/icontheme.h>
#include <glibmm/main.h>

SauronEyePanel::SauronEyePanel(std::shared_ptr<X11ScreenCapturer> capturer, 
                             std::shared_ptr<MqttClient> mqtt_client)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 5),
      screen_capturer_(capturer),
      windows_list_store_(Gtk::ListStore::create(windows_columns_)),
      mqtt_client_(mqtt_client)  // Reordered to match declaration order in header
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
        sigc::mem_fun(*this, &SauronEyePanel::on_tree_view_row_activated));
    windows_tree_view_.signal_button_press_event().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_window_button_press_event), false);
    
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
    screens_tree_view_.signal_row_activated().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_screens_row_activated));
    
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
    
    // Add delay option to options box
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
    
    // Start monitoring window events for automatic refresh
    if (screen_capturer_->start_window_events_monitoring()) {
        // Connect to the window list changed signal
        screen_capturer_->signal_window_list_changed().connect(
            sigc::mem_fun(*this, &SauronEyePanel::refresh_window_list));
        std::cout << "✅ Connected to window events for automatic refresh" << std::endl;
    } else {
        std::cout << "⚠️ Automatic window list refresh not available" << std::endl;
        // Fall back to periodic refresh
        auto_refresh_connection_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &SauronEyePanel::auto_refresh),
            auto_refresh_interval_sec_ * 1000);
    }
    
    show_all_children();
}

SauronEyePanel::~SauronEyePanel() {
    // Clean up the auto refresh timer if it's active
    if (auto_refresh_connection_.connected()) {
        auto_refresh_connection_.disconnect();
    }
    
    // Stop window event monitoring
    if (screen_capturer_ && screen_capturer_->is_monitoring_window_events()) {
        screen_capturer_->stop_window_events_monitoring();
    }
}

void SauronEyePanel::refresh_window_list() {
    auto selection = windows_tree_view_.get_selection();
    Gtk::TreeModel::iterator sel_iter = selection->get_selected();
    unsigned long preserved_id = 0;
    if (sel_iter) {
        preserved_id = (*sel_iter)[windows_columns_.m_col_id];
    }

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

        // Reselect if this was previously selected
        if (window_info.id == preserved_id) {
            selection->select(row);
        }
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

void SauronEyePanel::on_tree_view_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* /* column */) {
    Gtk::TreeModel::iterator iter = windows_list_store_->get_iter(path);
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        unsigned long window_id = row[windows_columns_.m_col_id];
        // Capture and immediately publish on double-click
        std::string filepath = take_capture("window", window_id);
        if (!filepath.empty() && mqtt_client_) {
            std::string topic = "sauron"; // Use unified MQTT topic
            bool use_base64 = true;
            // Add routing information to the JSON payload
            std::string routing = R"({"to": "agent", "from": "ui", "type": "image"})";
            mqtt_client_->publish_image(topic, filepath, routing, "window", use_base64);
        }
    }
}

void SauronEyePanel::on_screens_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* /* column */) {
    Gtk::TreeModel::iterator iter = screens_list_store_->get_iter(path);
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        unsigned long screen_id = row[screens_columns_.m_col_id];
        // Capture and immediately publish on double-click
        std::string filepath = take_capture("screen", screen_id);
        // Always publish via MQTT on double-click
        if (!filepath.empty() && mqtt_client_) {
            std::string topic = "sauron"; // Use unified MQTT topic
            bool use_base64 = true;
            // Add routing information to the JSON payload
            std::string routing = R"({"to": "agent", "from": "ui", "type": "image"})";
            mqtt_client_->publish_image(topic, filepath, routing, "screen", use_base64);
        }
    }
}

void SauronEyePanel::trigger_capture() {
    trigger_capture("manual");
}

void SauronEyePanel::trigger_capture(const std::string& /* trigger_type */) {
    auto selected_window = get_selected_window();
    
    if (selected_window) {
        take_capture("window", selected_window->id);
    } else {
        // No window selected, try to capture the first screen
        if (!screens_list_store_->children().empty()) {
            auto iter = screens_list_store_->children().begin();
            if (iter) {
                Gtk::TreeModel::Row row = *iter;
                unsigned long screen_id = row[screens_columns_.m_col_id];
                take_capture("screen", screen_id);
            }
        }
    }
}

void SauronEyePanel::perform_capture() {
    auto selected_window = get_selected_window();
    if (selected_window) {
        std::string filepath = take_capture("window", selected_window->id);
        if (!filepath.empty()) {
            std::cout << "Captured window to: " << filepath << std::endl;
        }
    } else {
        std::cerr << "No window selected for capture" << std::endl;
    }
}

std::optional<X11ScreenCapturer::WindowInfo> SauronEyePanel::get_selected_window() {
    auto selection = windows_tree_view_.get_selection();
    auto iter = selection->get_selected();
    if (!iter) return std::nullopt;
    auto row = *iter;
    unsigned long window_id = row[windows_columns_.m_col_id];
    // Directly lookup full WindowInfo from capturer
    auto windows = screen_capturer_->list_windows();
    for (const auto& w : windows) {
        if (w.id == window_id) {
            return w;
        }
    }
    return std::nullopt;
}

std::optional<X11ScreenCapturer::ScreenInfo> SauronEyePanel::get_selected_screen() {
    auto selection = screens_tree_view_.get_selection();
    auto iter = selection->get_selected();
    if (!iter) return std::nullopt;
    auto row = *iter;
    int screen_number = row[screens_columns_.m_col_id];
    // Directly lookup full ScreenInfo from capturer
    auto screens = screen_capturer_->detect_screens();
    for (const auto& s : screens) {
        if (s.number == screen_number) {
            return s;
        }
    }
    return std::nullopt;
}

std::string SauronEyePanel::generate_capture_filename(const std::string& type) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    // Format the filename with date and time
    std::stringstream ss;
    ss << "captures/";
    
    // Create directory if it doesn't exist
    std::filesystem::create_directories("captures");
    
    // Add timestamp to filename
    std::tm tm = *std::localtime(&time_t_now);
    ss << (type == "window" ? "window_" : "screen_")
       << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".png";
    
    return ss.str();
}

std::string SauronEyePanel::take_capture(const std::string& type, unsigned long id) {
    int delay_seconds = delay_spin_.get_value_as_int();
    std::optional<X11ScreenCapturer::WindowInfo> window_info_opt;

    if (delay_seconds > 0) {
        std::cout << "⏱️ Capture will start in " << delay_seconds << " seconds..." << std::endl;
        // Sleep for the specified delay
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }

    std::string filename = generate_capture_filename(type);
    std::string filepath = "";

    if (type == "window") {
        window_info_opt = get_selected_window();
        if (window_info_opt) {
            auto window_info = window_info_opt.value();
            if (screen_capturer_->capture_window(window_info, filename)) {
                filepath = std::filesystem::absolute(filename).string();
            } else {
                std::cerr << "❌ Failed to capture window" << std::endl;
                return "";
            }
        } else {
            std::cerr << "❌ No window selected" << std::endl;
            return "";
        }
    } else if (type == "screen") {
        auto screen_info_opt = get_selected_screen();
        if (screen_info_opt) {
            auto screen_info = screen_info_opt.value();
            if (screen_capturer_->capture_screen(screen_info.number, filename)) {
                filepath = std::filesystem::absolute(filename).string();
            } else {
                std::cerr << "❌ Failed to capture screen" << std::endl;
                return "";
            }
        } else {
            std::cerr << "❌ No screen selected" << std::endl;
            return "";
        }
    } else {
        std::cerr << "❌ Unknown capture type: " << type << std::endl;
        return "";
    }

    // Emit signals
    m_signal_capture_taken.emit(filepath);
    m_signal_capture_taken_extended.emit(filepath, type, std::to_string(id));

    // No internal MQTT publishing; SauronWindow will handle via signals

    return filepath;
}

bool SauronEyePanel::save_capture(const std::shared_ptr<Gdk::Pixbuf>& capture, const std::string& filename) {
    if (!capture) {
        std::cerr << "❌ Cannot save null capture" << std::endl;
        return false;
    }
    
    try {
        capture->save(filename, "png");
        std::cout << "✅ Saved capture to " << filename << std::endl;
        return true;
    } catch (const Glib::Error& e) {
        std::cerr << "❌ Failed to save capture: " << e.what() << std::endl;
        return false;
    }
}

bool SauronEyePanel::on_window_button_press_event(GdkEventButton* event) {
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        // Right-click detected
        Glib::RefPtr<Gtk::TreeSelection> selection = windows_tree_view_.get_selection();
        
        // Get tree path at the position of the right-click
        Gtk::TreeModel::Path path;
        Gtk::TreeViewColumn* column;
        int cell_x, cell_y;
        
        if (windows_tree_view_.get_path_at_pos(static_cast<int>(event->x), 
                                              static_cast<int>(event->y),
                                              path, column, cell_x, cell_y)) {
            // Select the row that was right-clicked
            selection->select(path);
            // Show context menu
            show_context_menu(event);
            return true;
        }
    }
    return false;
}

void SauronEyePanel::show_context_menu(GdkEventButton* event) {
    Gtk::Menu popup_menu;
    
    // Create menu items
    Gtk::MenuItem* capture_item = Gtk::manage(new Gtk::MenuItem("Capture Window"));
    capture_item->signal_activate().connect(
        sigc::mem_fun(*this, &SauronEyePanel::on_capture_window_clicked));
    popup_menu.append(*capture_item);
    
    // Add a separator
    popup_menu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
    
    // Copy option
    Gtk::MenuItem* copy_item = Gtk::manage(new Gtk::MenuItem("Copy Window ID"));
    copy_item->signal_activate().connect(sigc::mem_fun(*this, &SauronEyePanel::on_copy_window_id));
    popup_menu.append(*copy_item);
    
    popup_menu.show_all();
    popup_menu.popup(event->button, event->time);
}

void SauronEyePanel::on_copy_window_id() {
    Glib::RefPtr<Gtk::TreeSelection> selection = windows_tree_view_.get_selection();
    Gtk::TreeModel::iterator iter = selection->get_selected();
    
    if (iter) {
        Gtk::TreeModel::Row row = *iter;
        unsigned long window_id = row[windows_columns_.m_col_id];
        
        // Convert window_id to string and copy to clipboard
        Glib::RefPtr<Gtk::Clipboard> clipboard = Gtk::Clipboard::get();
        clipboard->set_text(std::to_string(window_id));
        
        std::cout << "📋 Window ID copied to clipboard: " << window_id << std::endl;
    }
}

// Helper for periodic refresh: refreshes window and screen lists
bool SauronEyePanel::auto_refresh() {
    refresh_window_list();
    refresh_screen_list();
    return true; // Continue calling
}