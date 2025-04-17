#ifndef SAURON_WINDOW_H
#define SAURON_WINDOW_H

#include <gtkmm.h>
#include "X11ScreenCapturer.h"
#include "MqttClient.h"
#include "SauronEyePanel.h"

class SauronWindow : public Gtk::Window {
public:
    SauronWindow();
    virtual ~SauronWindow();

protected:
    // Event handlers
    bool on_key_press_event(GdkEventKey* key_event) override;
    bool on_delete_event(GdkEventAny* event) override;

private:
    // Debug stream buffer for redirecting cout
    class DebugStreambuf : public std::streambuf {
    public:
        explicit DebugStreambuf(SauronWindow* window);
    protected:
        int_type overflow(int_type c) override;
    private:
        SauronWindow* window_;
        std::string buffer_;
    };

    // Core components
    std::shared_ptr<X11ScreenCapturer> capturer_;
    std::shared_ptr<MqttClient> mqtt_client_;
    SauronEyePanel sauron_eye_panel_;
    bool mqtt_connected_ = false;
    std::string last_capture_path_;

    // Settings persistence
    void load_settings();
    void save_settings();
    std::string orig_mqtt_host_;
    std::string orig_mqtt_port_;
    std::string orig_mqtt_topic_;
    std::string orig_mqtt_command_topic_;

    // Main layout
    Gtk::Box main_box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box content_box_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box left_panel_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box right_panel_{Gtk::ORIENTATION_VERTICAL};

    // MQTT settings panel
    Gtk::Frame mqtt_frame_{"MQTT Settings"};
    Gtk::Box mqtt_box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Entry mqtt_host_entry_;
    Gtk::Entry mqtt_port_entry_;
    Gtk::Entry mqtt_topic_entry_;
    Gtk::Entry mqtt_command_topic_entry_;
    Gtk::Label mqtt_command_topic_label_{"Command Topic"};
    Gtk::Label mqtt_status_label_;
    Gtk::Button mqtt_connect_button_;
    Gtk::Button send_button_;

    // Recent captures panel
    Gtk::Frame captures_frame_{"Recent Captures"};
    Gtk::Box captures_box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::ScrolledWindow captures_scroll_;
    Gtk::FlowBox captures_flow_;
    Gtk::Button open_folder_button_{"Open Folder"};

    // Debug output
    Gtk::ScrolledWindow debug_window_;
    Gtk::TextView debug_view_;
    Glib::RefPtr<Gtk::TextBuffer> debug_buffer_;
    DebugStreambuf* debug_streambuf_;
    std::streambuf* cout_buffer_;

    // Status bar
    Gtk::Statusbar status_bar_;

    // Helper methods
    void add_debug_text(const std::string& text);
    bool ensure_captures_directory();
    void refresh_captures();
    void add_thumbnail(const std::string& filepath);

    // Event handlers
    void on_capture_taken(const std::string& filename);
    void on_mqtt_connect_clicked();
    void on_thumbnail_clicked(const std::string& filepath);
    void on_thumbnail_activated_capture(const std::string& filepath);
    void on_open_folder_clicked();
    void on_send_clicked();
    void on_mqtt_message(const std::string& topic, const std::string& payload);

    // Helper methods
    void handle_capture_command();
    void handle_mqtt_command(const std::string& command);
    // Handle captures emitted by SauronEyePanel with extended signal
    void on_panel_capture(const std::string& filepath, const std::string& type, const std::string& id);
};

#endif // SAURON_WINDOW_H