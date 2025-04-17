#ifndef SAURON_EYE_PANEL_H
#define SAURON_EYE_PANEL_H

#include <gtkmm.h>
#include <string>
#include <memory>
#include <optional>
#include <chrono>
#include <filesystem>
#include "../include/X11ScreenCapturer.h"
#include "../include/MqttClient.h"
#include "../include/WindowColumns.h"

class SauronEyePanel : public Gtk::Box {
public:
    SauronEyePanel(std::shared_ptr<X11ScreenCapturer> capturer, 
                  std::shared_ptr<MqttClient> mqtt_client);
    virtual ~SauronEyePanel();
    
    // Signal accessors
    typedef sigc::signal<void, std::string> type_signal_capture_taken;
    type_signal_capture_taken signal_capture_taken() { return m_signal_capture_taken; }
    
    // Extended signal with additional parameters
    typedef sigc::signal<void, std::string, std::string, std::string> type_signal_capture_taken_extended;
    type_signal_capture_taken_extended signal_capture_taken_extended() { return m_signal_capture_taken_extended; }
    
    // Signal for when captures are saved
    typedef sigc::signal<void, std::string, std::string> type_signal_capture_saved;
    type_signal_capture_saved signal_capture_saved() { return m_signal_capture_saved; }
    
    // Trigger a capture programmatically (e.g., from MQTT command)
    void trigger_capture();
    void trigger_capture(const std::string& trigger_type);
    
protected:
    // Signal handlers
    void on_refresh_windows_clicked();
    void on_capture_window_clicked();
    void on_refresh_screens_clicked();
    void on_capture_screen_clicked();
    void on_tree_view_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
    void on_screens_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
    bool on_window_button_press_event(GdkEventButton* button_event);
    void show_context_menu(GdkEventButton* event);
    void on_copy_window_id();

private:
    // Helper methods
    void refresh_window_list();
    void refresh_screen_list();
    std::string take_capture(const std::string& type, unsigned long id);
    std::optional<X11ScreenCapturer::WindowInfo> get_selected_window();
    std::optional<X11ScreenCapturer::ScreenInfo> get_selected_screen();
    Glib::RefPtr<Gdk::Pixbuf> get_selected_screen_pixbuf();
    std::string generate_capture_filename(const std::string& type);
    bool save_capture(const std::shared_ptr<Gdk::Pixbuf>& capture, const std::string& filename);
    void save_capture(Glib::RefPtr<Gdk::Pixbuf> capture, const std::string& filename, const std::string& type, unsigned long id);
    void perform_capture();
    void execute_capture(const std::string& type, unsigned long id);
    
    // UI components
    Gtk::Frame capture_frame_;
    Gtk::Box capture_box_;
    
    // Windows section
    Gtk::Frame windows_frame_;
    Gtk::Box windows_box_;
    Gtk::ScrolledWindow windows_scrolled_window_;
    WindowColumns windows_columns_;
    Gtk::TreeView windows_tree_view_;
    Glib::RefPtr<Gtk::ListStore> windows_list_store_;
    Gtk::Button refresh_windows_button_;
    Gtk::Button capture_window_button_;
    
    // Screens section
    Gtk::Frame screens_frame_;
    Gtk::Box screens_box_;
    Gtk::ScrolledWindow screens_scrolled_window_;
    ScreenColumns screens_columns_;
    Gtk::TreeView screens_tree_view_;
    Glib::RefPtr<Gtk::ListStore> screens_list_store_;
    Gtk::Button refresh_screens_button_;
    Gtk::Button capture_screen_button_;
    
    // Capture options
    Gtk::Frame options_frame_;
    Gtk::Box options_box_;
    Gtk::Box delay_box_;  // Capture delay container
    Gtk::Label delay_label_;  // Capture delay label
    Gtk::SpinButton delay_spin_;  // Capture delay spin button
    
    // References to shared resources
    std::shared_ptr<X11ScreenCapturer> screen_capturer_;
    std::shared_ptr<MqttClient> mqtt_client_;
    
    // Signals
    type_signal_capture_taken m_signal_capture_taken;
    type_signal_capture_taken_extended m_signal_capture_taken_extended;
    type_signal_capture_saved m_signal_capture_saved;
};

#endif // SAURON_EYE_PANEL_H