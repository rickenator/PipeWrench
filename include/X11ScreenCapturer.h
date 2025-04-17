#ifndef X11_SCREEN_CAPTURER_H
#define X11_SCREEN_CAPTURER_H

#include <X11/Xlib.h>
#include <vector>
#include <string>

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
    
private:
    Display* display;
    
    bool save_image_to_png(XImage* image, const std::string& filename);
};

#endif // X11_SCREEN_CAPTURER_H