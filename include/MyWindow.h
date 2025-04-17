#ifndef SauronWindow_H
#define SauronWindow_H

#include <gtkmm.h>
#include <memory>
#include <vector>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>
#include "X11ScreenCapturer.h"
#include "MqttClient.h"
#include "SauronEyePanel.h"
#include "RecentCapturesPanel.h"

// Forward declarations
struct WindowInfo;

class SauronWindow : public Gtk::Window {
public:
    SauronWindow();
    virtual ~SauronWindow();

protected:
    void add_thumbnail(const std::string& filepath);
    void on_thumbnail_clicked(const std::string& filepath);

private:
    // Debug streambuf class
    class DebugStreambuf : public std::streambuf {
    public:
        DebugStreambuf(SauronWindow* window);
    protected:
        virtual int_type overflow(int_type c = traits_type::eof()) override;
    private:
        SauronWindow* window_;
        std::string buffer_;
    };

    // Shared resources
    std::shared_ptr<X11ScreenCapturer> capturer_;
    std::shared_ptr<MqttClient> mqtt_client_;
    std::vector<X11ScreenCapturer::WindowInfo> window_infos_;
    bool mqtt_connected_{false};
    std::string last_capture_path_;

    // MQTT Settings
    Gtk::Frame mqtt_frame_{"MQTT Settings"};
    Gtk::Box mqtt_box_{Gtk::ORIENTATION_VERTICAL, 5};
    Gtk::Entry mqtt_host_entry_;
    Gtk::Entry mqtt_port_entry_;
    Gtk::Entry mqtt_topic_entry_;
    Gtk::Button mqtt_connect_button_;
    Gtk::Label mqtt_status_label_;

    // Recent Captures Panel
    Gtk::Box captures_box_{Gtk::ORIENTATION_VERTICAL, 5};
    Gtk::Frame captures_frame_{"Recent Captures"};
    Gtk::ScrolledWindow captures_scroll_;
    Gtk::FlowBox captures_flow_;
    Gtk::Button open_folder_button_{"Open Captures Folder"};

    // Main UI container
    Gtk::Box main_box_{Gtk::ORIENTATION_VERTICAL, 5};
    Gtk::Box content_box_{Gtk::ORIENTATION_HORIZONTAL, 5};  // For side-by-side layout
    Gtk::Box left_panel_{Gtk::ORIENTATION_VERTICAL, 5};     // For capture controls
    Gtk::Box right_panel_{Gtk::ORIENTATION_VERTICAL, 5};    // For MQTT and recent captures

    // Window capture panel and controls
    SauronEyePanel sauron_eye_panel_;
    Gtk::Button send_button_;  // Added Send button

    // Debug view
    Gtk::ScrolledWindow debug_window_;
    Gtk::TextView debug_view_;
    Glib::RefPtr<Gtk::TextBuffer> debug_buffer_;
    std::streambuf* cout_buffer_{nullptr};
    DebugStreambuf* debug_streambuf_{nullptr};

    // Status bar at bottom
    Gtk::Statusbar status_bar_;

    // Signal handlers
    void on_mqtt_connect_clicked();
    void on_send_clicked();  // Added handler for Send button
    void on_capture_taken(const std::string& filename);
    void on_thumbnail_activated_capture(const std::string& filepath);
    void on_open_folder_clicked();
    void refresh_captures();
    bool ensure_captures_directory();
    bool on_key_press_event(GdkEventKey* key_event) override;
    bool on_delete_event(GdkEventAny* event) override;

    // Debug output helper
    void add_debug_text(const std::string& text);
};

#endif // SauronWindow_H