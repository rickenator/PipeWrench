#ifndef X11_SCREEN_CAPTURER_H
#define X11_SCREEN_CAPTURER_H

#include <X11/Xlib.h>
#include <vector>
#include <string>
#include <functional>
#include <sigc++/signal.h>
#include <sigc++/connection.h>
#include <glibmm/main.h>

class X11ScreenCapturer {
public:
    struct WindowInfo {
        Window id;
        std::string title;
        int x, y;
        unsigned int width, height;
        bool has_decorations;
        bool is_visible;
    };
    
    struct ScreenInfo {
        int number;
        std::string name;
        int x, y;
        unsigned int width, height;
    };
    
    X11ScreenCapturer();
    ~X11ScreenCapturer();
    
    std::vector<WindowInfo> list_windows();
    std::vector<ScreenInfo> detect_screens();
    bool capture_window(const WindowInfo& window, const std::string& filename);
    XImage* capture_window_image(const WindowInfo& window);
    bool capture_screen(int screen_number, const std::string& filename);
    XImage* capture_screen_image(int screen_number);
    
    // Window event monitoring functions
    bool start_window_events_monitoring();
    void stop_window_events_monitoring();
    bool is_monitoring_window_events() const;
    
    // Signal for window list change events
    typedef sigc::signal<void> type_signal_window_list_changed;
    type_signal_window_list_changed signal_window_list_changed() { return m_signal_window_list_changed; }
    
private:
    Display* display;
    bool monitoring_window_events_;
    sigc::connection event_check_connection_;
    
    // X11 event handling
    bool process_x11_events();
    void register_for_window_events();
    
    // Window list change signal
    type_signal_window_list_changed m_signal_window_list_changed;
    
    bool save_image_to_png(XImage* image, const std::string& filename);
};

#endif // X11_SCREEN_CAPTURER_H