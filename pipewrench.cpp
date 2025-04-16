// pipewrench.cpp

#include <iostream>
#include <glibmm/variant.h>
#include <giomm/dbusproxy.h>
#include <giomm.h>
#include <gtkmm.h>
#include <gdkmm/window.h> // For Gdk::Window
#include <gdk/gdk.h>      // For GDK backend check (optional but good)
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>     // For gdk_x11_window_get_xid
#endif
#ifdef GDK_WINDOWING_WAYLAND // Add include for Wayland check
#include <gdk/gdkwayland.h> // For gdk_wayland_display_get_type, GDK_IS_WAYLAND_DISPLAY
#endif
#include <vector>
#include <uuid/uuid.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xcomposite.h>
#include <filesystem>
#include <sys/stat.h>
#include <gtkmm/liststore.h>
#include <gtkmm/treeview.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/button.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/frame.h>
#include <gtkmm/statusbar.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/checkbutton.h>
#include <ctime>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
extern "C" {
#include <glib.h>
#include <gio/gio.h>
}

Glib::ustring generate_token() {
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);
    return Glib::ustring::compose("tok_%1", uuid_str);
}

Glib::VariantContainerBase wrap_variant(GVariant* variant) {
    return Glib::VariantContainerBase(variant, true);
}

// Function to ensure the captures directory exists
bool ensure_captures_directory() {
    const std::string capturesDir = "captures";
    
    // Check if directory exists using filesystem (C++17)
    if (!std::filesystem::exists(capturesDir)) {
        std::cout << "📁 Captures directory doesn't exist, creating it..." << std::endl;
        try {
            // Create the directory
            if (!std::filesystem::create_directory(capturesDir)) {
                std::cerr << "❌ Failed to create captures directory" << std::endl;
                return false;
            }
            std::cout << "✅ Created captures directory successfully" << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "❌ Filesystem error creating directory: " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

// X11-based screen capture functionality with improved window detection
class X11ScreenCapturer {
public:
    // Structure to represent a window
    struct WindowInfo {
        Window id;
        std::string title;
        std::string wm_class;  // Added WM_CLASS for better window identification
        int x, y, width, height;
        bool visible;
        bool has_wm_class;     // Flag to indicate if WM_CLASS is available
        bool has_decorations;   // Flag to indicate if this is the decorated version
        Window parent;          // Parent window ID for hierarchy detection
        std::string window_type; // For debugging: "normal", "decorated", "undecorated", etc.
    };
    
    X11ScreenCapturer() : display_(nullptr), width_(0), height_(0), root_(0), pixmap_(0) {
        initialize();
    }
    
    ~X11ScreenCapturer() {
        cleanup();
    }
    
    bool initialize() {
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            std::cerr << "❌ Failed to open X11 display" << std::endl;
            return false;
        }
        
        int screen = DefaultScreen(display_);
        root_ = RootWindow(display_, screen);
        
        // Get screen dimensions
        width_ = DisplayWidth(display_, screen);
        height_ = DisplayHeight(display_, screen);
        
        std::cout << "✅ X11 Screen Capturer initialized with resolution: " 
                  << width_ << "x" << height_ << std::endl;
        return true;
    }
    
    void cleanup() {
        if (pixmap_) {
            XFreePixmap(display_, pixmap_);
            pixmap_ = 0;
        }
        
        if (display_) {
            XCloseDisplay(display_);
            display_ = nullptr;
        }
    }
    
    // Get list of all windows on the display with improved detection
    std::vector<WindowInfo> list_windows() {
        std::vector<WindowInfo> windows;
        
        if (!display_) {
            std::cerr << "❌ X11 display not initialized" << std::endl;
            return windows;
        }
        
        // For debugging
        std::cout << "🔍 Looking for windows using multiple methods..." << std::endl;
        
        // Method 1: Get windows via XQueryTree (top-level only)
        windows = get_windows_via_query_tree();
        
        // Method 2: Get active windows from _NET_CLIENT_LIST
        std::vector<WindowInfo> net_client_windows = get_windows_from_net_client_list();
        
        // Method 3: Recursively search for child windows with content
        std::vector<WindowInfo> deep_windows = get_windows_recursive(root_, 0);
        
        // Merge the results, prioritizing windows from _NET_CLIENT_LIST
        std::map<Window, bool> added_windows;
        std::vector<WindowInfo> merged_windows;
        
        // First add windows from _NET_CLIENT_LIST (most reliable for apps)
        for (const auto& win : net_client_windows) {
            if (win.width > 50 && win.height > 50) {
                merged_windows.push_back(win);
                added_windows[win.id] = true;
            }
        }
        
        // Then add from XQueryTree if not already added
        for (const auto& win : windows) {
            if (!added_windows[win.id] && win.width > 50 && win.height > 50) {
                merged_windows.push_back(win);
                added_windows[win.id] = true;
            }
        }
        
        // Finally add any missing windows from recursive search
        for (const auto& win : deep_windows) {
            if (!added_windows[win.id] && win.width > 50 && win.height > 50 && 
                (!win.title.empty() || !win.wm_class.empty())) {
                merged_windows.push_back(win);
                added_windows[win.id] = true;
            }
        }
        
        // Debug output
        std::cout << "📊 Found " << merged_windows.size() << " unique windows total (before filtering)" << std::endl;
        
        return merged_windows;
    }
    
    // Get windows using the _NET_CLIENT_LIST property (EWMH standard)
    std::vector<WindowInfo> get_windows_from_net_client_list() {
        std::vector<WindowInfo> windows;
        
        // Get _NET_CLIENT_LIST property
        Atom net_client_list = XInternAtom(display_, "_NET_CLIENT_LIST", True);
        Atom actual_type;
        int actual_format;
        unsigned long n_items, bytes_after;
        unsigned char* prop = nullptr;
        
        if (net_client_list != None &&
            XGetWindowProperty(display_, root_, net_client_list, 0, 1024, False, 
                              AnyPropertyType, &actual_type, &actual_format, 
                              &n_items, &bytes_after, &prop) == Success && 
            prop != nullptr) {
            
            Window* client_list = reinterpret_cast<Window*>(prop);
            std::cout << "📋 Found " << n_items << " windows in _NET_CLIENT_LIST" << std::endl;
            
            for (unsigned long i = 0; i < n_items; i++) {
                WindowInfo info = get_window_info(client_list[i]);
                
                // Add with a special tag to identify source
                if (info.visible) {
                    info.window_type = "EWMH";
                    windows.push_back(info);
                    std::cout << "  🪟 EWMH Window: \"" << info.title 
                            << "\" Class: \"" << info.wm_class
                            << "\" (" << info.width << "x" << info.height 
                            << " at " << info.x << "," << info.y << ")" << std::endl;
                }
            }
            
            XFree(prop);
        } else {
            std::cout << "⚠️ _NET_CLIENT_LIST property not found" << std::endl;
        }
        
        return windows;
    }
    
    // Recursively find windows (helps find terminal windows and others)
    std::vector<WindowInfo> get_windows_recursive(Window window, int depth) {
        std::vector<WindowInfo> results;
        
        // Avoid going too deep (prevent infinite recursion and focus on useful windows)
        if (depth > 3) {
            return results;
        }
        
        WindowInfo info = get_window_info(window);
        
        // Add current window if it has a title or class and seems to be a real window
        if (info.visible && ((!info.title.empty() || info.has_wm_class) && 
            info.width > 50 && info.height > 50)) {
            results.push_back(info);
        }
        
        // Get child windows
        Window root_return, parent_return;
        Window* children = nullptr;
        unsigned int num_children = 0;
        
        Status status = XQueryTree(display_, window, &root_return, &parent_return, 
                                  &children, &num_children);
        
        if (status != 0 && children != nullptr) {
            for (unsigned int i = 0; i < num_children; i++) {
                // Recursively get info for each child
                std::vector<WindowInfo> child_windows = get_windows_recursive(children[i], depth + 1);
                results.insert(results.end(), child_windows.begin(), child_windows.end());
            }
            
            XFree(children);
        }
        
        return results;
    }
    
    // Get windows using standard XQueryTree (similar to original implementation)
    std::vector<WindowInfo> get_windows_via_query_tree() {
        std::vector<WindowInfo> windows;
        
        // Get the root window children
        Window root_return, parent_return;
        Window* children = nullptr;
        unsigned int num_children = 0;
        
        Status status = XQueryTree(display_, root_, &root_return, &parent_return, 
                                  &children, &num_children);
        
        if (status == 0 || children == nullptr) {
            std::cerr << "❌ Failed to query window tree" << std::endl;
            return windows;
        }
        
        std::cout << "🪟 Found " << num_children << " top-level windows" << std::endl;
        
        // Process each window
        for (unsigned int i = 0; i < num_children; i++) {
            WindowInfo info = get_window_info(children[i]);
            
            // Only include visible windows with title or class and reasonable size
            if (info.visible && ((!info.title.empty() || info.has_wm_class) && 
                info.width > 50 && info.height > 50)) {
                
                // Skip the "mutter guard window" or other whole-screen windows if they match exactly
                if (info.width == width_ && info.height == height_ && 
                    (info.title.find("mutter") != std::string::npos || 
                     info.wm_class.find("mutter") != std::string::npos)) {
                    std::cout << "  ⚠️ Skipping probable full-screen guard window: " 
                              << info.title << " (" << info.wm_class << ")" << std::endl;
                    continue;
                }
                
                windows.push_back(info);
                std::cout << "  🪟 Window: \"" << info.title 
                          << "\" Class: \"" << info.wm_class
                          << "\" (" << info.width << "x" << info.height 
                          << " at " << info.x << "," << info.y << ")" << std::endl;
            }
        }
        
        // Free the children list
        if (children) {
            XFree(children);
        }
        
        return windows;
    }
    
    // Capture a specific window with improved methods for Wayland compatibility
    bool capture_window(const WindowInfo& window, const std::string& output_filename) {
        if (!display_) {
            std::cerr << "❌ X11 display not initialized" << std::endl;
            return false;
        }
        
        std::cout << "🔍 Attempting to capture window: \"" << window.title 
                  << "\" Class: \"" << window.wm_class
                  << "\" (" << window.width << "x" << window.height << ")" << std::endl;
        
        try {
            // Create a pixmap to draw into
            int depth = DefaultDepth(display_, DefaultScreen(display_));
            if (pixmap_) {
                XFreePixmap(display_, pixmap_);
            }
            pixmap_ = XCreatePixmap(display_, root_, window.width, window.height, depth);
            
            // Create a graphics context
            GC gc = XCreateGC(display_, pixmap_, 0, nullptr);
            
            // Fill with a pattern to detect if copying worked
            XSetForeground(display_, gc, 0x000000FF); // Blue background to detect successful copies
            XFillRectangle(display_, pixmap_, gc, 0, 0, window.width, window.height);
            
            // Make sure the window is mapped and viewable
            XWindowAttributes attr;
            if (XGetWindowAttributes(display_, window.id, &attr) && attr.map_state == IsViewable) {
                // Try multiple capture methods
                XImage* image = nullptr;
                bool capture_success = false;
                
                // Method 1: Try XComposite first - works better with Wayland XWayland windows
                int composite_event_base, composite_error_base;
                if (XCompositeQueryExtension(display_, &composite_event_base, &composite_error_base)) {
                    std::cout << "  🧩 Trying XComposite capture method..." << std::endl;
                    
                    try {
                        // Use XComposite to get a window pixmap
                        XCompositeRedirectWindow(display_, window.id, CompositeRedirectAutomatic);
                        XSync(display_, False);
                        
                        Pixmap window_pixmap = XCompositeNameWindowPixmap(display_, window.id);
                        
                        if (window_pixmap) {
                            // Try to copy from the window pixmap
                            XCopyArea(display_, window_pixmap, pixmap_, gc, 
                                    0, 0, window.width, window.height, 0, 0);
                            
                            XFreePixmap(display_, window_pixmap);
                            
                            // Get the image
                            image = XGetImage(display_, pixmap_, 
                                            0, 0, window.width, window.height, 
                                            AllPlanes, ZPixmap);
                            
                            // Check if the capture worked (not all blue)
                            if (image && !is_image_solid_color(image, 0x000000FF)) {
                                capture_success = true;
                                std::cout << "  ✅ XComposite capture succeeded!" << std::endl;
                            } else if (image) {
                                std::cout << "  ❌ XComposite returned blank or solid color image" << std::endl;
                                XDestroyImage(image);
                                image = nullptr;
                            }
                        }
                        
                        // Clean up
                        XCompositeUnredirectWindow(display_, window.id, CompositeRedirectAutomatic);
                        XSync(display_, False);
                    } catch (...) {
                        std::cerr << "  ⚠️ Exception in XComposite capture, trying next method" << std::endl;
                        XCompositeUnredirectWindow(display_, window.id, CompositeRedirectAutomatic);
                        XSync(display_, False);
                    }
                }
                
                // Method 2: Standard XCopyArea if XComposite failed
                if (!capture_success) {
                    std::cout << "  🔄 Trying standard XCopyArea method..." << std::endl;
                    
                    // Fill pixmap with pattern again
                    XSetForeground(display_, gc, 0x0000FF00); // Green background
                    XFillRectangle(display_, pixmap_, gc, 0, 0, window.width, window.height);
                    
                    // Copy window to pixmap
                    XCopyArea(display_, window.id, pixmap_, gc, 
                            0, 0, window.width, window.height, 0, 0);
                    
                    // Get the image
                    if (image) {
                        XDestroyImage(image);
                    }
                    image = XGetImage(display_, pixmap_, 
                                     0, 0, window.width, window.height, 
                                     AllPlanes, ZPixmap);
                    
                    // Check if capture worked
                    if (image && !is_image_solid_color(image, 0x0000FF00)) {
                        capture_success = true;
                        std::cout << "  ✅ XCopyArea capture succeeded!" << std::endl;
                    } else if (image) {
                        std::cout << "  ❌ XCopyArea returned blank or solid color image" << std::endl;
                        XDestroyImage(image);
                        image = nullptr;
                    }
                }
                
                // Method 3: Direct XGetImage as a last resort
                if (!capture_success) {
                    std::cout << "  🔄 Trying direct XGetImage method..." << std::endl;
                    
                    if (image) {
                        XDestroyImage(image);
                    }
                    
                    // Try direct capture from window
                    try {
                        image = XGetImage(display_, window.id, 
                                        0, 0, window.width, window.height, 
                                        AllPlanes, ZPixmap);
                        
                        if (image && !is_image_all_black(image)) {
                            capture_success = true;
                            std::cout << "  ✅ Direct XGetImage capture succeeded!" << std::endl;
                        } else if (image) {
                            std::cout << "  ❌ Direct XGetImage returned black or empty image" << std::endl;
                            XDestroyImage(image);
                            image = nullptr;
                        }
                    } catch (...) {
                        std::cerr << "  ⚠️ Exception in direct XGetImage capture" << std::endl;
                    }
                }
                
                // Check final capture result
                if (!image) {
                    std::cerr << "❌ All capture methods failed" << std::endl;
                    XFreeGC(display_, gc);
                    return false;
                }
                
                std::cout << "📸 Window captured successfully: " << window.width << "x" << window.height 
                          << " with depth " << image->depth << std::endl;
                
                // Save the image to a file
                bool save_result = save_image_as_png(image, output_filename);
                
                // Clean up
                XDestroyImage(image);
                XFreeGC(display_, gc);
                
                return save_result;
            } else {
                std::cerr << "❌ Window is not viewable" << std::endl;
                XFreeGC(display_, gc);
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "❌ Exception during window capture: " << e.what() << std::endl;
            return false;
        } catch (...) {
            std::cerr << "❌ Unknown exception during window capture" << std::endl;
            return false;
        }
    }
    
    // Save XImage as PNG file
    bool save_image_as_png(XImage* image, const std::string& filename) {
        if (!image) {
            std::cerr << "❌ No image to save" << std::endl;
            return false;
        }
        
        // Create a Cairo surface from the XImage data
        cairo_surface_t* surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, 
            image->width, 
            image->height
        );
        
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "❌ Failed to create Cairo surface" << std::endl;
            return false;
        }
        
        // Get the surface data to copy pixels into
        unsigned char* surface_data = cairo_image_surface_get_data(surface);
        int stride = cairo_image_surface_get_stride(surface);
        
        // This assumes 24-bit (RGB) or 32-bit (RGBA) XImage
        if (image->depth == 24 || image->depth == 32) {
            // Lock the surface for writing
            cairo_surface_flush(surface);
            
            // Copy the image data with pixel format conversion
            for (int y = 0; y < image->height; y++) {
                for (int x = 0; x < image->width; x++) {
                    unsigned long pixel = XGetPixel(image, x, y);
                    int index = y * stride + x * 4;
                    
                    // BGR to ARGB conversion
                    surface_data[index + 0] = (pixel & 0x000000FF) >> 0;  // Blue
                    surface_data[index + 1] = (pixel & 0x0000FF00) >> 8;  // Green
                    surface_data[index + 2] = (pixel & 0x00FF0000) >> 16; // Red
                    surface_data[index + 3] = 0xFF;                       // Alpha (fully opaque)
                }
            }
            
            // Mark the surface as modified
            cairo_surface_mark_dirty(surface);
            
            // Determine the file type based on extension
            if (filename.find(".jpg") != std::string::npos || 
                filename.find(".jpeg") != std::string::npos) {
                // Save as JPEG (note: cairo doesn't have native JPEG support)
                // Fall back to PNG for now
                std::cout << "⚠️ JPEG format requested, but falling back to PNG (Cairo limitation)" << std::endl;
                cairo_status_t status = cairo_surface_write_to_png(surface, filename.c_str());
                
                if (status != CAIRO_STATUS_SUCCESS) {
                    std::cerr << "❌ Failed to save image: " << cairo_status_to_string(status) << std::endl;
                    cairo_surface_destroy(surface);
                    return false;
                }
            } else {
                // Save as PNG
                cairo_status_t status = cairo_surface_write_to_png(surface, filename.c_str());
                
                if (status != CAIRO_STATUS_SUCCESS) {
                    std::cerr << "❌ Failed to save image: " << cairo_status_to_string(status) << std::endl;
                    cairo_surface_destroy(surface);
                    return false;
                }
            }
            
            std::cout << "💾 Image saved successfully to: " << filename << std::endl;
            cairo_surface_destroy(surface);
            return true;
        } else {
            std::cerr << "❌ Unsupported image depth: " << image->depth << std::endl;
            cairo_surface_destroy(surface);
            return false;
        }
    }

    // Add method to get root window info for full screen capture
    WindowInfo get_root_window_info() {
        WindowInfo root_info;
        root_info.id = root_;
        root_info.title = "Full Screen";
        root_info.wm_class = "Screen";
        root_info.width = width_;
        root_info.height = height_;
        root_info.x = 0;
        root_info.y = 0;
        root_info.visible = true;
        root_info.has_wm_class = true;
        return root_info;
    }
    
private:
    Display* display_;
    int width_;
    int height_;
    Window root_;
    Pixmap pixmap_;
    
    // Check if an image is all black
    bool is_image_all_black(XImage* image) {
        if (!image) return true;
        
        const int sample_step = std::max(1, std::min(image->width, image->height) / 20);
        
        for (int y = 0; y < image->height; y += sample_step) {
            for (int x = 0; x < image->width; x += sample_step) {
                unsigned long pixel = XGetPixel(image, x, y);
                if (pixel != 0) {
                    return false;
                }
            }
        }
        return true;
    }
    
    // Check if an image is a solid color
    bool is_image_solid_color(XImage* image, unsigned long color) {
        if (!image) return true;
        
        const int sample_step = std::max(1, std::min(image->width, image->height) / 20);
        int different_count = 0;
        
        for (int y = 0; y < image->height; y += sample_step) {
            for (int x = 0; x < image->width; x += sample_step) {
                unsigned long pixel = XGetPixel(image, x, y);
                if (pixel != color) {
                    different_count++;
                    if (different_count > 5) {  // Allow a few different pixels for noise
                        return false;
                    }
                }
            }
        }
        return true;
    }
    
    // Get comprehensive information about a window
    WindowInfo get_window_info(Window window) {
        WindowInfo info;
        info.id = window;
        info.visible = false;
        info.has_wm_class = false;
        
        // Get window attributes
        XWindowAttributes attrs;
        Status status = XGetWindowAttributes(display_, window, &attrs);
        
        if (status == 0) {
            return info;
        }
        
        // Get window geometry
        info.x = attrs.x;
        info.y = attrs.y;
        info.width = attrs.width;
        info.height = attrs.height;
        
        // Check if window is visible
        info.visible = (attrs.map_state == IsViewable);
        
        // Get window name using EWMH _NET_WM_NAME (UTF-8 title)
        Atom net_wm_name = XInternAtom(display_, "_NET_WM_NAME", False);
        Atom utf8_string = XInternAtom(display_, "UTF8_STRING", False);
        Atom actual_type_return;
        int actual_format_return;
        unsigned long nitems_return;
        unsigned long bytes_after_return;
        unsigned char *prop_return = nullptr;
        
        if (XGetWindowProperty(display_, window, net_wm_name, 0, 1024, False, utf8_string,
                              &actual_type_return, &actual_format_return, &nitems_return,
                              &bytes_after_return, &prop_return) == Success && 
            prop_return != nullptr) {
            
            info.title = reinterpret_cast<char*>(prop_return);
            XFree(prop_return);
        } else {
            // Fall back to WM_NAME
            char* window_name = nullptr;
            if (XFetchName(display_, window, &window_name) && window_name) {
                info.title = window_name;
                XFree(window_name);
            } else {
                // Try to get WM_NAME property as another fallback
                XTextProperty text_prop;
                if (XGetWMName(display_, window, &text_prop) && text_prop.value) {
                    info.title = reinterpret_cast<char*>(text_prop.value);
                    XFree(text_prop.value);
                } else {
                    info.title = "";
                }
            }
        }
        
        // Get window class (helps identify applications like terminals)
        XClassHint class_hint;
        if (XGetClassHint(display_, window, &class_hint)) {
            info.has_wm_class = true;
            if (class_hint.res_name) {
                info.wm_class = class_hint.res_name;
                XFree(class_hint.res_name);
            }
            if (class_hint.res_class) {
                if (!info.wm_class.empty()) {
                    info.wm_class += ".";
                }
                info.wm_class += class_hint.res_class;
                XFree(class_hint.res_class);
            }
        } else {
            info.wm_class = "";
        }
        
        return info;
    }

    // Detect and identify duplicate windows (decorated vs undecorated)
    std::vector<WindowInfo> detect_window_duplicates(std::vector<WindowInfo>& windows) {
        std::vector<WindowInfo> filtered_windows;
        std::map<std::string, std::vector<WindowInfo*>> title_groups;
        
        // First, group windows by title
        for (auto& window : windows) {
            // Skip windows with empty titles or mutter guard windows
            if (window.title.empty() || 
                window.title.find("mutter") != std::string::npos ||
                window.wm_class.find("mutter") != std::string::npos) {
                continue;
            }
            
            title_groups[window.title].push_back(&window);
        }
        
        // Process each group of windows with the same title
        for (auto& [title, group] : title_groups) {
            if (group.size() == 1) {
                // Only one window with this title, add it as is
                group[0]->window_type = "normal";
                filtered_windows.push_back(*group[0]);
            } else {
                // Multiple windows with same title - likely decorated and undecorated versions
                // Sort by size (usually decorated is larger)
                std::sort(group.begin(), group.end(), 
                    [](const WindowInfo* a, const WindowInfo* b) {
                        return (a->width * a->height) > (b->width * b->height);
                    });
                
                // Check if they look like decorated/undecorated pairs
                if (group.size() == 2) {
                    WindowInfo* larger = group[0];
                    WindowInfo* smaller = group[1];
                    
                    // If the larger window contains the smaller window, they're likely decorated/undecorated versions
                    if (larger->x <= smaller->x && larger->y <= smaller->y &&
                        larger->x + larger->width >= smaller->x + smaller->width &&
                        larger->y + larger->height >= smaller->y + smaller->height) {
                        
                        // Mark the windows appropriately
                        larger->window_type = "decorated";
                        larger->has_decorations = true;
                        smaller->window_type = "undecorated";
                        smaller->has_decorations = false;
                        
                        // Add both to the filtered list
                        filtered_windows.push_back(*larger);
                        filtered_windows.push_back(*smaller);
                    } else {
                        // Can't determine if they're related, add both with generic labels
                        for (auto* window : group) {
                            window->window_type = "unknown";
                            filtered_windows.push_back(*window);
                        }
                    }
                } else {
                    // More than 2 windows with same title, add all with index
                    for (size_t i = 0; i < group.size(); i++) {
                        group[i]->window_type = "variant_" + std::to_string(i+1);
                        filtered_windows.push_back(*group[i]);
                    }
                }
            }
        }
        
        return filtered_windows;
    }
};

// --- PipeWrench Class Definition ---
class PipeWrench {
public:
    PipeWrench();
    void handle_create_session_response(const Glib::RefPtr<Gio::AsyncResult>& result);
    void handle_request_response(const Glib::VariantContainerBase& parameters);
    void select_sources();
    void start_session();
    void open_pipewire_remote();
    void cleanup();
    
    // Add direct X11 capture alternative
    bool try_direct_capture();

    Glib::RefPtr<Gio::DBus::Proxy> portal_proxy_;
    Glib::ustring session_handle_token_;

private:
    Glib::RefPtr<Gio::DBus::Proxy> session_proxy_;
    Glib::RefPtr<Gio::DBus::Connection> dbus_connection_;
    Glib::ustring session_handle_;
    void subscribe_to_session_signal();
    void install_raw_signal_listener();
    
    // Add X11 capturer
    X11ScreenCapturer x11_capturer_;
};

// --- PipeWrench Member Function Implementations ---
PipeWrench::PipeWrench() {
    try {
        // Get the session bus
        dbus_connection_ = Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION);
        
        // Verify the connection
        if (!dbus_connection_) {
            std::cerr << "❌ Failed to connect to session bus" << std::endl;
            return;
        }
        
        // Create the portal proxy
        portal_proxy_ = Gio::DBus::Proxy::create_sync(
            dbus_connection_,
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast");
        
        if (!portal_proxy_) {
            std::cerr << "❌ Failed to create portal proxy" << std::endl;
            return;
        }
        
        std::cout << "🎯 Portal proxy created successfully." << std::endl;
        
        // Check if the ScreenCast interface is available via introspection
        try {
            // Create an empty parameters tuple more explicitly
            std::vector<Glib::VariantBase> empty_params;
            auto var_params = Glib::VariantContainerBase::create_tuple(empty_params);
            
            auto introspection = dbus_connection_->call_sync(
                "/org/freedesktop/portal/desktop", 
                "org.freedesktop.DBus.Introspectable", 
                "Introspect",
                var_params,
                "org.freedesktop.portal.Desktop");
            
            Glib::Variant<Glib::ustring> xml_data;
            introspection.get_child(xml_data);
            Glib::ustring xml = xml_data.get();
            
            if (xml.find("org.freedesktop.portal.ScreenCast") != Glib::ustring::npos) {
                std::cout << "✅ ScreenCast interface is available" << std::endl;
            } else {
                std::cerr << "⚠️ ScreenCast interface is NOT available in the introspection data" << std::endl;
            }
        } catch (const Glib::Error& e) {
            std::cerr << "⚠️ Error introspecting portal: " << e.what() << std::endl;
        }
        
        install_raw_signal_listener();
    } catch (const Glib::Error& e) {
        std::cerr << "❌ Error initializing PipeWrench: " << e.what() << std::endl;
    }
}

void PipeWrench::install_raw_signal_listener() {
    std::cout << "📻 Installing raw signal listener on session path wildcard..." << std::endl;
    g_dbus_connection_signal_subscribe(
        dbus_connection_->gobj(),
        "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Session",
        "Response",
        nullptr, // match all paths
        nullptr, // match all arg0
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection* connection,
           const gchar* sender_name,
           const gchar* object_path,
           const gchar* interface_name,
           const gchar* signal_name,
           GVariant* parameters,
           gpointer user_data) {
            std::cout << "📡 GDBus Signal Caught:" << std::endl;
            std::cout << "   Sender:   " << sender_name << std::endl;
            std::cout << "   Path:     " << object_path << std::endl;
            std::cout << "   Interface:" << interface_name << std::endl;
            std::cout << "   Signal:   " << signal_name << std::endl;

            gchar* debug_str = g_variant_print(parameters, TRUE);
            std::cout << "   Params:   " << debug_str << std::endl;
            g_free(debug_str);
        },
        nullptr,
        nullptr);
}

void PipeWrench::cleanup() {
    std::cerr << "🧹 Cleaning up resources." << std::endl;
}

void PipeWrench::select_sources() {
    std::cout << "🖼️ select_sources() called." << std::endl;
}

void PipeWrench::start_session() {
    std::cout << "▶️ start_session() called." << std::endl;
}

void PipeWrench::open_pipewire_remote() {
    std::cout << "📡 open_pipewire_remote() called." << std::endl;
}

void PipeWrench::subscribe_to_session_signal() {
    std::cout << "📶 subscribe_to_session_signal() called." << std::endl;
}

void PipeWrench::handle_create_session_response(const Glib::RefPtr<Gio::AsyncResult>& result) {
    std::cout << "🔔 Entered handle_create_session_response" << std::endl;
    try {
        auto reply = portal_proxy_->call_finish(result);
        std::cout << "✔️ Call finished, processing variant..." << std::endl;

        if (reply.get_n_children() == 0) {
            std::cerr << "⚠️ No children in reply variant." << std::endl;
            cleanup();
            return;
        }

        auto variant_base = reply.get_child(0);
        std::cout << "📥 Raw variant received: " << variant_base.print() << std::endl;

        if (!variant_base.is_container()) {
            std::cerr << "⚠️ Variant is not a container. Type: " << variant_base.get_type_string() << std::endl;
            cleanup();
            return;
        }

        using TupleType = std::tuple<uint32_t, std::map<Glib::ustring, Glib::VariantBase>>;
        auto typed_variant = Glib::VariantBase::cast_dynamic<Glib::Variant<TupleType>>(variant_base);
        auto tuple = typed_variant.get();
        auto response_map = std::get<1>(tuple);

        std::cout << "🧵 Parsed session response map with keys:";
        for (const auto& [k, v] : response_map) {
            std::cout << "\n  [" << k << "] => " << v.print();
        }
        std::cout << std::endl;

        std::cout << "🔖 Session token used: " << session_handle_token_ << std::endl;

        GVariantBuilder dict_builder;
        g_variant_builder_init(&dict_builder, G_VARIANT_TYPE_VARDICT);
        for (const auto& [key, val] : response_map) {
            std::cout << "🧩 Adding to builder: " << key << " => " << val.print() << std::endl;
            g_variant_builder_add(&dict_builder, "{sv}", key.c_str(), const_cast<GVariant*>(val.gobj()));
        }

        GVariant* raw_variant = g_variant_builder_end(&dict_builder);
        Glib::VariantContainerBase container = wrap_variant(raw_variant);
        std::cout << "✔️ Wrapped variant container created. Forwarding to request handler." << std::endl;

        handle_request_response(container);

    } catch (const Glib::Error& ex) {
        std::cerr << "💥 Exception in CreateSession handler: " << ex.what() << std::endl;
        cleanup();
    } catch (const std::exception& ex) {
        std::cerr << "💣 STD exception: " << ex.what() << std::endl;
        cleanup();
    }
}

void PipeWrench::handle_request_response(const Glib::VariantContainerBase& parameters) {
    std::cout << "🔔 handle_request_response called — parsing parameters..." << std::endl;

    try {
        auto session_variant = Glib::VariantBase::cast_dynamic<Glib::Variant<std::map<Glib::ustring, Glib::VariantBase>>>(parameters);
        auto response_map = session_variant.get();

        std::cout << "📨 Response Map Parsed:" << std::endl;
        for (const auto& [key, val] : response_map) {
            std::cout << "  [" << key << "] => " << val.print() << std::endl;
        }

        auto it = response_map.find("session_handle");
        if (it != response_map.end()) {
            session_handle_ = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(it->second).get();
            std::cout << "✅ session_handle was found in response map." << std::endl;
        } else {
            std::cerr << "⚠️ Missing session_handle in response. Reconstructing manually." << std::endl;
            session_handle_ = "/org/freedesktop/portal/desktop/session/1_0/" + session_handle_token_;
        }
        std::cout << "🔗 Session handle resolved as: " << session_handle_ << std::endl;

        subscribe_to_session_signal();
        select_sources();
        start_session();
        open_pipewire_remote();
    } catch (const Glib::Error& e) {
        std::cerr << "🚨 handle_request_response error: " << e.what() << std::endl;
        cleanup();
    }
}

// Add direct capture method
bool PipeWrench::try_direct_capture() {
    std::cout << "🎬 Attempting direct X11 screen capture..." << std::endl;
    
    // Create a test window info for capturing the entire screen
    X11ScreenCapturer::WindowInfo root_window = x11_capturer_.get_root_window_info();
    
    // Generate a filename with timestamp
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
    
    // Capture the screen to a file
    std::string filename = "captures/screen_" + std::string(timestamp) + ".png";
    return x11_capturer_.capture_window(root_window, filename);
}

// --- MyWindow Class Definition (Needs to be before main) ---
class MyWindow : public Gtk::Window {
public:
    MyWindow() : window_capturer(), box(Gtk::ORIENTATION_VERTICAL, 5) {
        set_title("PipeWrench - Window Capture Tool");
        set_default_size(700, 500);
        set_border_width(10);
        
        // Create the top label
        Gtk::Label* header_label = Gtk::manage(new Gtk::Label("Select a window to capture:"));
        header_label->set_xalign(0);
        header_label->set_margin_bottom(5);
        
        // Create UI components
        scrolled_window.add(tree_view);
        scrolled_window.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        scrolled_window.set_min_content_height(300);
        
        // Set up the list store
        list_store = Gtk::ListStore::create(columns);
        tree_view.set_model(list_store);
        
        // Add columns to the tree view
        tree_view.append_column("Window ID", columns.col_id);
        tree_view.append_column("Title", columns.col_title);
        tree_view.append_column("Type", columns.col_window_type);
        tree_view.append_column("Dimensions", columns.col_dimensions);
        tree_view.append_column("Position", columns.col_position);
        
        // Make columns resizable
        for (int i = 0; i < 5; i++) {
            Gtk::TreeView::Column* column = tree_view.get_column(i);
            if (column) {
                column->set_resizable(true);
                column->set_sizing(Gtk::TREE_VIEW_COLUMN_AUTOSIZE);
                if (i == 1) { // Title column
                    column->set_min_width(200);
                    column->set_expand(true);
                }
            }
        }
        
        // Create checkbox for window decorations
        decorations_check.set_label("Include window decorations");
        decorations_check.set_active(false);
        
        // Create action buttons
        refresh_button.set_label("Refresh Window List");
        capture_button.set_label("Capture Selected Window");
        capture_button.set_sensitive(false); // Disabled until a window is selected
        
        // Create format selection
        format_label.set_text("Output Format:");
        format_combo.append("png", "PNG");
        format_combo.append("jpg", "JPEG");
        format_combo.set_active_id("png");
        
        // Create options box
        Gtk::Box* options_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
        options_box->pack_start(decorations_check, false, false);
        
        // Create button box for controls
        Gtk::Box* controls_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
        controls_box->pack_start(refresh_button, false, false);
        controls_box->pack_start(capture_button, false, false);
        controls_box->pack_end(format_combo, false, false);
        controls_box->pack_end(format_label, false, false);
        
        // Create status bar
        status_bar.push("Ready. No window selected.");
        
        // Pack all widgets into the main box
        box.pack_start(*header_label, false, false);
        box.pack_start(scrolled_window, true, true);
        box.pack_start(*options_box, false, false);
        box.pack_start(*controls_box, false, false);
        box.pack_start(status_bar, false, false);
        
        // Add the box to the window
        add(box);
        
        // Connect signals
        refresh_button.signal_clicked().connect(
            sigc::mem_fun(*this, &MyWindow::on_refresh_clicked));
        
        capture_button.signal_clicked().connect(
            sigc::mem_fun(*this, &MyWindow::on_capture_clicked));
        
        tree_view.get_selection()->signal_changed().connect(
            sigc::mem_fun(*this, &MyWindow::on_selection_changed));
        
        decorations_check.signal_toggled().connect(
            sigc::mem_fun(*this, &MyWindow::on_decorations_toggled));
        
        // Add double-click support for the tree view
        tree_view.signal_row_activated().connect(
            sigc::mem_fun(*this, &MyWindow::on_row_activated));
        
        // Show all widgets
        show_all_children();
        
        // Populate window list initially
        populate_window_list();
        
        std::cout << "  MyWindow constructor finished." << std::endl;
    }
    
    virtual ~MyWindow() {
        std::cout << "  MyWindow destructor called." << std::endl;
    }

protected:
    // Signal handlers
    void on_refresh_clicked() {
        populate_window_list();
        status_bar.push("Window list refreshed.");
    }
    
    void on_decorations_toggled() {
        populate_window_list();
        status_bar.push("Window decoration preference updated.");
    }
    
    void on_capture_clicked() {
        Glib::RefPtr<Gtk::TreeSelection> selection = tree_view.get_selection();
        Gtk::TreeModel::iterator iter = selection->get_selected();
        
        if (!iter) {
            status_bar.push("No window selected for capture.");
            return;
        }
        
        // Get the window info pointer from the selected row
        Gtk::TreeModel::Row row = *iter;
        X11ScreenCapturer::WindowInfo* window_info = row[columns.col_window_info_ptr];
        
        if (!window_info) {
            status_bar.push("Invalid window information.");
            return;
        }
        
        // Ensure captures directory exists
        if (!ensure_captures_directory()) {
            status_bar.push("Failed to ensure captures directory exists.");
            return;
        }
        
        // Generate a filename with timestamp
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        
        std::string format = format_combo.get_active_id();
        std::string filename = "captures/window_" + std::string(timestamp) + "." + format;
        
        // Attempt to capture the window
        status_bar.push("Capturing window: " + window_info->title);
        
        if (window_capturer.capture_window(*window_info, filename)) {
            status_bar.push("Window captured successfully: " + filename);
        } else {
            status_bar.push("Failed to capture window.");
        }
    }
    
    void on_selection_changed() {
        Glib::RefPtr<Gtk::TreeSelection> selection = tree_view.get_selection();
        Gtk::TreeModel::iterator iter = selection->get_selected();
        
        if (iter) {
            Gtk::TreeModel::Row row = *iter;
            Glib::ustring window_title = row[columns.col_title];
            Glib::ustring window_type = row[columns.col_window_type];
            status_bar.push("Selected: " + window_title + " (" + window_type + ")");
            capture_button.set_sensitive(true);
        } else {
            status_bar.push("No window selected.");
            capture_button.set_sensitive(false);
        }
    }
    
    void on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column) {
        Gtk::TreeModel::iterator iter = list_store->get_iter(path);
        if (!iter) {
            status_bar.push("Invalid row activated.");
            return;
        }
        
        Gtk::TreeModel::Row row = *iter;
        X11ScreenCapturer::WindowInfo* window_info = row[columns.col_window_info_ptr];
        
        if (!window_info) {
            status_bar.push("Invalid window information.");
            return;
        }
        
        // Ensure captures directory exists
        if (!ensure_captures_directory()) {
            status_bar.push("Failed to ensure captures directory exists.");
            return;
        }
        
        // Generate a filename with timestamp
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        
        std::string format = format_combo.get_active_id();
        std::string filename = "captures/window_" + std::string(timestamp) + "." + format;
        
        // Attempt to capture the window
        status_bar.push("Capturing window: " + window_info->title);
        
        if (window_capturer.capture_window(*window_info, filename)) {
            status_bar.push("Window captured successfully: " + filename);
        } else {
            status_bar.push("Failed to capture window.");
        }
    }
    
    // Populate the window list with all windows, including duplicates for analysis
    void populate_window_list() {
        list_store->clear();
        window_infos.clear();
        
        // Get list of windows from X11ScreenCapturer
        std::vector<X11ScreenCapturer::WindowInfo> windows = window_capturer.list_windows();
        
        // Store the window infos to keep them alive
        window_infos = windows;
        
        bool prefer_decorated = decorations_check.get_active();
        
        // First pass: add all windows to the list without filtering
        // This helps with analysis to see what windows are available
        for (size_t i = 0; i < window_infos.size(); i++) {
            X11ScreenCapturer::WindowInfo& info = window_infos[i];
            
            // Skip windows with empty titles and very small windows
            if ((info.title.empty() && info.wm_class.empty()) || 
                (info.width < 10 || info.height < 10)) {
                continue;
            }
            
            // Add window to the list
            Gtk::TreeModel::Row row = *(list_store->append());
            row[columns.col_id] = Glib::ustring::format(info.id);
            row[columns.col_title] = info.title.empty() ? ("Class: " + info.wm_class) : info.title;
            row[columns.col_window_type] = info.window_type.empty() ? "Unknown" : info.window_type;
            row[columns.col_dimensions] = Glib::ustring::format(info.width, "×", info.height);
            row[columns.col_position] = Glib::ustring::format("(", info.x, ",", info.y, ")");
            row[columns.col_window_info_ptr] = &info;
        }
        
        std::cout << "  Added " << list_store->children().size() << " windows to the list." << std::endl;
    }
    
    bool on_delete_event(GdkEventAny* event) override {
        std::cout << "  MyWindow delete event." << std::endl;
        return Gtk::Window::on_delete_event(event);
    }

private:
    // UI components
    Gtk::Box box;
    Gtk::ScrolledWindow scrolled_window;
    Gtk::TreeView tree_view;
    Glib::RefPtr<Gtk::ListStore> list_store;
    Gtk::Button refresh_button;
    Gtk::Button capture_button;
    Gtk::CheckButton decorations_check;
    Gtk::Label format_label;
    Gtk::ComboBoxText format_combo;
    Gtk::Statusbar status_bar;
    
    // Capture functionality
    X11ScreenCapturer window_capturer;
    std::vector<X11ScreenCapturer::WindowInfo> window_infos;
    
    // Window columns class with added window_type column
    class WindowColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        WindowColumns() {
            add(col_id);
            add(col_title);
            add(col_window_type);
            add(col_dimensions);
            add(col_position);
            add(col_window_info_ptr);
        }

        Gtk::TreeModelColumn<Glib::ustring> col_id;
        Gtk::TreeModelColumn<Glib::ustring> col_title;
        Gtk::TreeModelColumn<Glib::ustring> col_window_type;
        Gtk::TreeModelColumn<Glib::ustring> col_dimensions;
        Gtk::TreeModelColumn<Glib::ustring> col_position;
        Gtk::TreeModelColumn<X11ScreenCapturer::WindowInfo*> col_window_info_ptr;
    };
    
    WindowColumns columns;
};

// --- Main Function ---
int main(int argc, char* argv[]) {
    // Ensure captures directory exists
    ensure_captures_directory();

    std::cout << "🔧 Initializing Gtk::Application..." << std::endl;
    auto app = Gtk::Application::create(argc, argv, "org.example.PipeWrench");
    std::cout << "✅ Gtk::Application created." << std::endl;

    // Connect the activate signal to a lambda that creates the window
    app->signal_activate().connect([app]() {
        // Create the window when the application is activated
        auto window = new MyWindow();
        app->add_window(*window);
        window->show();
    });
    
    std::cout << "🚀 Running application event loop (app->run())..." << std::endl;
    int status = app->run();
    std::cout << "🏁 Application event loop finished. Exit status: " << status << std::endl;

    return status;
}

