#ifndef SAURON_EYE_PANEL_H
#define SAURON_EYE_PANEL_H

#include <gtkmm.h>
#include <string>
#include <memory>
#include "../include/X11ScreenCapturer.h"
#include "../include/MqttClient.h"
#include "../include/WindowColumns.h"

class SauronEyePanel : public Gtk::Box {
public:
    SauronEyePanel(std::shared_ptr<X11ScreenCapturer> capturer, 
                  std::shared_ptr<MqttClient> mqtt_client);
    virtual ~SauronEyePanel();
    
    // Signal accessors
    sigc::signal<void, std::string>& signal_capture_taken() { return m_signal_capture_taken; }
    
private:
    // UI components
    Gtk::Frame capture_frame_;
    Gtk::Box capture_box_;
    
    // Windows section
    Gtk::Frame windows_frame_;
    Gtk::Box windows_box_;
    Gtk::ScrolledWindow windows_scrolled_window_;
    WindowColumns windows_columns_; // moved before list_store_
    Gtk::TreeView windows_tree_view_;
    Glib::RefPtr<Gtk::ListStore> windows_list_store_;
    Gtk::Button refresh_windows_button_;
    Gtk::Button capture_window_button_;
    
    // Screens section
    Gtk::Frame screens_frame_;
    Gtk::Box screens_box_;
    Gtk::ScrolledWindow screens_scrolled_window_;
    ScreenColumns screens_columns_; // moved before list_store_
    Gtk::TreeView screens_tree_view_;
    Glib::RefPtr<Gtk::ListStore> screens_list_store_;
    Gtk::Button refresh_screens_button_;
    Gtk::Button capture_screen_button_;
    
    // Capture options
    Gtk::Frame options_frame_;
    Gtk::Box options_box_;
    Gtk::CheckButton include_cursor_check_;
    Gtk::CheckButton publish_mqtt_check_;
    Gtk::CheckButton publish_base64_check_;
    // Add MQTT topic entry
    Gtk::Label mqtt_topic_label_;
    Gtk::Entry mqtt_topic_entry_;
    Gtk::Box delay_box_;
    Gtk::Label delay_label_;
    Gtk::SpinButton delay_spin_;
    
    // References to shared resources
    std::shared_ptr<X11ScreenCapturer> screen_capturer_;
    std::shared_ptr<MqttClient> mqtt_client_;
    
    // Signals
    sigc::signal<void, std::string> m_signal_capture_taken;
    
    // Helper methods
    void refresh_window_list();
    void refresh_screen_list();
    void on_refresh_windows_clicked();
    void on_capture_window_clicked();
    void on_refresh_screens_clicked();
    void on_capture_screen_clicked();
    void on_window_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*); // Double-click handler
    std::string take_capture(const std::string& type, unsigned long window_id = 0);
};

#endif // SAURON_EYE_PANEL_H