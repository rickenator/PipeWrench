#include "../include/X11ScreenCapturer.h"

#include <iostream>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/Xatom.h>  // For X11 property atoms
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <algorithm>
#include <filesystem>
#include <sys/stat.h>
#include <glibmm/main.h>  // For Glib::signal_timeout

X11ScreenCapturer::X11ScreenCapturer() : monitoring_window_events_(false) {
    display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "❌ Failed to open X display" << std::endl;
    }
}

X11ScreenCapturer::~X11ScreenCapturer() {
    // Stop monitoring before closing display
    stop_window_events_monitoring();
    
    if (display) {
        XCloseDisplay(display);
    }
}

std::vector<X11ScreenCapturer::WindowInfo> X11ScreenCapturer::list_windows() {
    std::vector<WindowInfo> windows;
    
    if (!display) {
        std::cerr << "❌ No X display connection" << std::endl;
        return windows;
    }
    
    // Get the root window
    Window root = DefaultRootWindow(display);
    
    // Get all top-level windows
    Window root_return, parent_return;
    Window* children;
    unsigned int num_children;
    
    Status status = XQueryTree(display, root, &root_return, &parent_return, &children, &num_children);
    
    if (status == 0) {
        std::cerr << "❌ Failed to query window tree" << std::endl;
        return windows;
    }
    
    // Filter windows to get only those with titles
    for (unsigned int i = 0; i < num_children; i++) {
        WindowInfo info;
        info.id = children[i];
        
        // Get window attributes
        XWindowAttributes attrs;
        if (XGetWindowAttributes(display, info.id, &attrs)) {
            info.width = attrs.width;
            info.height = attrs.height;
            info.x = attrs.x;
            info.y = attrs.y;
            info.is_visible = (attrs.map_state == IsViewable);
            
            // Skip invisible windows
            if (!info.is_visible) {
                continue;
            }
            
            // Get window title
            char* window_name = nullptr;
            if (XFetchName(display, info.id, &window_name) && window_name) {
                info.title = window_name;
                XFree(window_name);
            } else {
                // Try to get WM_NAME property if XFetchName fails
                XTextProperty text_prop;
                if (XGetWMName(display, info.id, &text_prop) && text_prop.value) {
                    info.title = reinterpret_cast<char*>(text_prop.value);
                    XFree(text_prop.value);
                } else {
                    info.title = "[Unnamed Window]";
                }
            }
            
            // Skip windows without meaningful titles
            if (info.title.empty() || info.title == "[Unnamed Window]") {
                continue;
            }
            
            windows.push_back(info);
        }
    }
    
    XFree(children);
    
    // Sort windows by title
    std::sort(windows.begin(), windows.end(), 
             [](const WindowInfo& a, const WindowInfo& b) {
                 return a.title < b.title;
             });
    
    return windows;
}

std::vector<X11ScreenCapturer::ScreenInfo> X11ScreenCapturer::detect_screens() {
    std::vector<ScreenInfo> screens;
    
    if (!display) {
        std::cerr << "❌ No X display connection" << std::endl;
        return screens;
    }
    
    // Get default screen
    int screen_count = ScreenCount(display);
    
    // Add "all screens" option
    ScreenInfo all_screens;
    all_screens.number = -1;
    all_screens.name = "All Screens";
    all_screens.x = 0;
    all_screens.y = 0;
    all_screens.width = DisplayWidth(display, DefaultScreen(display));
    all_screens.height = DisplayHeight(display, DefaultScreen(display));
    screens.push_back(all_screens);
    
    // Get XRandR extension information for multi-monitor support
    int xrandr_event_base, xrandr_error_base;
    if (XRRQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
        // Get screen resources
        XRRScreenResources* resources = XRRGetScreenResources(display, DefaultRootWindow(display));
        if (resources) {
            for (int i = 0; i < resources->noutput; i++) {
                XRROutputInfo* output_info = XRRGetOutputInfo(display, resources, resources->outputs[i]);
                
                if (output_info->connection == RR_Connected) {
                    // Find the CRTC for this output
                    if (output_info->crtc) {
                        XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(display, resources, output_info->crtc);
                        
                        if (crtc_info) {
                            ScreenInfo screen;
                            screen.number = i;
                            screen.name = output_info->name;
                            screen.x = crtc_info->x;
                            screen.y = crtc_info->y;
                            screen.width = crtc_info->width;
                            screen.height = crtc_info->height;
                            
                            screens.push_back(screen);
                            
                            XRRFreeCrtcInfo(crtc_info);
                        }
                    }
                }
                
                XRRFreeOutputInfo(output_info);
            }
            
            XRRFreeScreenResources(resources);
        }
    } else {
        // Fallback if XRandR is not available - add each screen
        for (int i = 0; i < screen_count; i++) {
            ScreenInfo screen;
            screen.number = i;
            screen.name = "Screen " + std::to_string(i);
            screen.x = 0;
            screen.y = 0;
            screen.width = DisplayWidth(display, i);
            screen.height = DisplayHeight(display, i);
            
            screens.push_back(screen);
        }
    }
    
    return screens;
}

bool X11ScreenCapturer::capture_window(const WindowInfo& window, const std::string& filename) {
    if (!display) {
        std::cerr << "❌ No X display connection" << std::endl;
        return false;
    }
    
    // Get the window image
    XImage* image = capture_window_image(window);
    if (!image) {
        std::cerr << "❌ Failed to capture window image" << std::endl;
        return false;
    }
    
    // Save to PNG using Cairo
    bool result = save_image_to_png(image, filename);
    
    // Free the image
    XDestroyImage(image);
    
    return result;
}

XImage* X11ScreenCapturer::capture_window_image(const WindowInfo& window) {
    if (!display) {
        std::cerr << "❌ No X display connection" << std::endl;
        return nullptr;
    }
    
    // Get window attributes
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, window.id, &attrs)) {
        std::cerr << "❌ Failed to get window attributes" << std::endl;
        return nullptr;
    }
    
    // Check if window is visible or minimized
    bool is_minimized = (attrs.map_state != IsViewable);
    if (is_minimized) {
        std::cout << "⚠️ Window is minimized or hidden, capturing screen area instead" << std::endl;
    }
    
    // Get the root window for capturing
    Window root = DefaultRootWindow(display);
    
    // Log the window dimensions we're capturing
    std::cout << "📸 Capturing window: \"" << window.title << "\" (" << window.width << "×" << window.height << ")" << std::endl;
    
    XImage* image = nullptr;
    
    if (!is_minimized) {
        // First try to use XComposite for active windows, which gives better results
        try {
            // Use XComposite to redirect the window
            XCompositeRedirectWindow(display, window.id, CompositeRedirectAutomatic);
            XSync(display, False);
            
            // Create a Pixmap to hold the window contents
            Pixmap pixmap = XCompositeNameWindowPixmap(display, window.id);
            if (pixmap) {
                // Get the window image from the pixmap
                image = XGetImage(display, pixmap, 0, 0, attrs.width, attrs.height, AllPlanes, ZPixmap);
                
                // Free the pixmap as we don't need it anymore
                XFreePixmap(display, pixmap);
                
                if (image) {
                    std::cout << "✅ Successfully captured window using XComposite method" << std::endl;
                } else {
                    std::cerr << "⚠️ Failed to get window image from pixmap, falling back to root window method" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "⚠️ Exception during XComposite capture: " << e.what() << std::endl;
        }
    }
    
    // Fallback method: Capture from root window
    // This works for both visible and minimized windows (gets the area where the window is/would be)
    if (!image) {
        image = XGetImage(display, root, 
                         window.x, window.y, 
                         window.width, window.height, 
                         AllPlanes, ZPixmap);
        
        if (image) {
            std::cout << "✅ Successfully captured " << (is_minimized ? "area where window would be" : "window") 
                      << " using root window method" << std::endl;
        } else {
            std::cerr << "❌ Failed to get window image using root window method" << std::endl;
        }
    }
    
    if (image) {
        std::cout << "✅ Final window image dimensions: " << image->width << "×" << image->height << std::endl;
    }
    
    return image;
}

bool X11ScreenCapturer::capture_screen(int screen_number, const std::string& filename) {
    if (!display) {
        std::cerr << "❌ No X display connection" << std::endl;
        return false;
    }
    
    // Get the screen image
    XImage* image = capture_screen_image(screen_number);
    if (!image) {
        std::cerr << "❌ Failed to capture screen image" << std::endl;
        return false;
    }
    
    // Save to PNG using Cairo
    bool result = save_image_to_png(image, filename);
    
    // Free the image
    XDestroyImage(image);
    
    return result;
}

XImage* X11ScreenCapturer::capture_screen_image(int screen_number) {
    if (!display) {
        std::cerr << "❌ No X display connection" << std::endl;
        return nullptr;
    }
    
    // Get screen information
    Window root = DefaultRootWindow(display);
    int width, height;
    int x_offset = 0, y_offset = 0;
    
    // Get XRandR information for multi-monitor support
    std::vector<ScreenInfo> screens = detect_screens();
    
    // Find the requested screen
    const ScreenInfo* target_screen = nullptr;
    if (screen_number >= 0) {
        // Find the specific screen
        for (const auto& screen : screens) {
            if (screen.number == screen_number) {
                target_screen = &screen;
                break;
            }
        }
        
        if (!target_screen) {
            std::cerr << "❌ Screen number " << screen_number << " not found" << std::endl;
            return nullptr;
        }
        
        width = target_screen->width;
        height = target_screen->height;
        x_offset = target_screen->x;
        y_offset = target_screen->y;
        
        std::cout << "📸 Capturing screen " << screen_number << ": " << target_screen->name 
                  << " at position (" << x_offset << "," << y_offset << ") with size " 
                  << width << "x" << height << std::endl;
    } else {
        // Capture all screens (full desktop)
        width = DisplayWidth(display, DefaultScreen(display));
        height = DisplayHeight(display, DefaultScreen(display));
        std::cout << "📸 Capturing full desktop with size " << width << "x" << height << std::endl;
    }
    
    // Ensure valid dimensions
    if (width <= 0 || height <= 0) {
        std::cerr << "❌ Invalid screen dimensions: " << width << "x" << height << std::endl;
        return nullptr;
    }

    // Check for permissions to capture the screen
    int xrandr_event_base, xrandr_error_base;
    if (!XCompositeQueryExtension(display, &xrandr_event_base, &xrandr_error_base)) {
        std::cerr << "❌ XComposite extension not available. Screen capture may not be supported." << std::endl;
        return nullptr;
    }

    // Get the screen image
    XImage* image = nullptr;
    try {
        // Use XGetImage to capture the root window (desktop)
        image = XGetImage(display, root, x_offset, y_offset, width, height, AllPlanes, ZPixmap);

        if (!image) {
            std::cerr << "❌ Failed to get screen image. Ensure the application has the necessary permissions." << std::endl;
            return nullptr;
        } else {
            std::cout << "✅ Successfully captured screen image" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during screen capture: " << e.what() << std::endl;
        return nullptr;
    }
    
    return image;
}

bool X11ScreenCapturer::save_image_to_png(XImage* image, const std::string& filename) {
    if (!image) {
        std::cerr << "❌ No image to save" << std::endl;
        return false;
    }
    
    // Create Cairo surface
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, image->width, image->height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "❌ Failed to create Cairo surface: " << cairo_status_to_string(cairo_surface_status(surface)) << std::endl;
        return false;
    }
    
    // Get Cairo surface data
    unsigned char* data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    
    // Copy image data to Cairo surface
    for (int y = 0; y < image->height; y++) {
        for (int x = 0; x < image->width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            
            // X11 returns pixel values in platform's native format, need to extract ARGB
            unsigned char a = 0xFF; // Fully opaque
            unsigned char r = (pixel >> 16) & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char b = pixel & 0xFF;
            
            // Cairo uses premultiplied alpha in ARGB format
            unsigned char* p = data + y * stride + x * 4;
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = a;
        }
    }
    
    // Mark the surface as dirty after modification
    cairo_surface_mark_dirty(surface);
    
    // Ensure directory exists
    std::string dir_path = filename.substr(0, filename.find_last_of('/'));
    if (!dir_path.empty()) {
        std::string mkdir_cmd = "mkdir -p " + dir_path;
        system(mkdir_cmd.c_str());
    }
    
    // Write to PNG
    cairo_status_t status = cairo_surface_write_to_png(surface, filename.c_str());
    
    // Clean up
    cairo_surface_destroy(surface);
    
    if (status != CAIRO_STATUS_SUCCESS) {
        std::cerr << "❌ Failed to write image to PNG: " << cairo_status_to_string(status) << std::endl;
        return false;
    }
    
    std::cout << "✅ Image saved to PNG: " << filename << std::endl;
    return true;
}

bool X11ScreenCapturer::start_window_events_monitoring() {
    if (!display) {
        std::cerr << "❌ Cannot start monitoring window events: No X display connection" << std::endl;
        return false;
    }
    
    if (monitoring_window_events_) {
        // Already monitoring
        return true;
    }
    
    // Register for window property change and structure notifications
    register_for_window_events();
    
    // Start a timer to check for X11 events
    event_check_connection_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &X11ScreenCapturer::process_x11_events),
        100); // Check every 100ms
    
    monitoring_window_events_ = true;
    std::cout << "✅ Started monitoring window events" << std::endl;
    
    return true;
}

void X11ScreenCapturer::stop_window_events_monitoring() {
    if (!monitoring_window_events_) {
        return;
    }
    
    // Disconnect the event check timer
    if (event_check_connection_.connected()) {
        event_check_connection_.disconnect();
    }
    
    monitoring_window_events_ = false;
    std::cout << "✅ Stopped monitoring window events" << std::endl;
}

bool X11ScreenCapturer::is_monitoring_window_events() const {
    return monitoring_window_events_;
}

void X11ScreenCapturer::register_for_window_events() {
    if (!display) {
        return;
    }
    
    Window root = DefaultRootWindow(display);
    
    // Select events on the root window
    XSelectInput(display, root, SubstructureNotifyMask | PropertyChangeMask);
    
    // Also select events on all existing windows
    Window root_return, parent_return;
    Window* children;
    unsigned int num_children;
    
    if (XQueryTree(display, root, &root_return, &parent_return, &children, &num_children)) {
        for (unsigned int i = 0; i < num_children; i++) {
            XSelectInput(display, children[i], PropertyChangeMask | StructureNotifyMask);
        }
        XFree(children);
    }
    
    // Flush to ensure events are registered
    XFlush(display);
}

bool X11ScreenCapturer::process_x11_events() {
    if (!display || !monitoring_window_events_) {
        return false;  // Stop this timer
    }
    
    bool window_list_changed = false;
    
    // Check for pending X events
    while (XPending(display)) {
        XEvent event;
        XNextEvent(display, &event);
        
        // Handle different event types
        switch (event.type) {
            case CreateNotify:
                std::cout << "🪟 Window created: " << event.xcreatewindow.window << std::endl;
                window_list_changed = true;
                break;
                
            case DestroyNotify:
                std::cout << "🪟 Window destroyed: " << event.xdestroywindow.window << std::endl;
                window_list_changed = true;
                break;
                
            case PropertyNotify:
                // Only care about property changes that would affect the window list
                if (event.xproperty.atom == XInternAtom(display, "WM_NAME", False) ||
                    event.xproperty.atom == XInternAtom(display, "_NET_WM_NAME", False)) {
                    std::cout << "🪟 Window title changed" << std::endl;
                    window_list_changed = true;
                }
                break;
                
            case MapNotify:
                std::cout << "🪟 Window mapped (shown): " << event.xmap.window << std::endl;
                window_list_changed = true;
                break;
                
            case UnmapNotify:
                std::cout << "🪟 Window unmapped (hidden): " << event.xunmap.window << std::endl;
                window_list_changed = true;
                break;
        }
    }
    
    // Emit signal if window list changed
    if (window_list_changed) {
        m_signal_window_list_changed.emit();
    }
    
    return true;  // Continue this timer
}