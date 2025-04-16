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
#include <jpeglib.h> // Added JPEG library support
#include <algorithm> // For std::sort
#include <chrono>    // For timestamp comparison
#include <dirent.h>  // For directory listing
extern "C" {
#include <glib.h>
#include <gio/gio.h>
}

// Forward declarations
class MyWindow;
class RecentCapturesPanel;

// X11ScreenCapturer implementation
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
    
    X11ScreenCapturer() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            std::cerr << "❌ Failed to open X display" << std::endl;
        }
    }
    
    ~X11ScreenCapturer() {
        if (display) {
            XCloseDisplay(display);
        }
    }
    
    std::vector<WindowInfo> list_windows() {
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
    
    std::vector<ScreenInfo> detect_screens() {
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
    
    bool capture_window(const WindowInfo& window, const std::string& filename) {
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
    
    XImage* capture_window_image(const WindowInfo& window) {
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
        
        // Check if window is visible
        if (attrs.map_state != IsViewable) {
            std::cerr << "❌ Window is not viewable" << std::endl;
            return nullptr;
        }
        
        // Make sure window exists
        XCompositeRedirectWindow(display, window.id, CompositeRedirectAutomatic);
        XSync(display, False);
        
        // Get the window image
        XImage* image = XGetImage(display, window.id, 0, 0, attrs.width, attrs.height, AllPlanes, ZPixmap);
        if (!image) {
            std::cerr << "❌ Failed to get window image" << std::endl;
            return nullptr;
        }
        
        return image;
    }
    
    bool capture_screen(int screen_number, const std::string& filename) {
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
    
    XImage* capture_screen_image(int screen_number) {
        if (!display) {
            std::cerr << "❌ No X display connection" << std::endl;
            return nullptr;
        }
        
        // Get screen information
        Window root = DefaultRootWindow(display);
        int width, height;
        
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
        } else {
            // Capture all screens (full desktop)
            width = DisplayWidth(display, DefaultScreen(display));
            height = DisplayHeight(display, DefaultScreen(display));
        }
        
        // Get the screen image
        XImage* image = XGetImage(display, root, 
                                 target_screen ? target_screen->x : 0, 
                                 target_screen ? target_screen->y : 0, 
                                 width, height, AllPlanes, ZPixmap);
        if (!image) {
            std::cerr << "❌ Failed to get screen image" << std::endl;
            return nullptr;
        }
        
        return image;
    }
    
private:
    Display* display;
    
    bool save_image_to_png(XImage* image, const std::string& filename) {
        if (!image) {
            std::cerr << "❌ No image to save" << std::endl;
            return false;
        }
        
        // Create Cairo surface
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, image->width, image->height);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            std::cerr << "❌ Failed to create Cairo surface" << std::endl;
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
};

// Define a struct to store information about captured files
struct CaptureInfo {
    std::string filename;
    std::string timestamp;
    std::string type; // "window" or "screen"
    std::string dimensions;
    std::string source_name;
    std::filesystem::file_time_type file_time;
};

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

// Save image as JPEG file
bool save_image_as_jpeg(XImage* image, const std::string& filename, int quality = 90) {
    if (!image) {
        std::cerr << "❌ No image to save as JPEG" << std::endl;
        return false;
    }
    
    FILE* outfile = fopen(filename.c_str(), "wb");
    if (!outfile) {
        std::cerr << "❌ Error opening JPEG output file: " << filename << std::endl;
        return false;
    }
    
    // Set up JPEG compression structures
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);
    
    // Set image parameters
    cinfo.image_width = image->width;
    cinfo.image_height = image->height;
    cinfo.input_components = 3; // RGB
    cinfo.in_color_space = JCS_RGB;
    
    // Set defaults and quality
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    
    // Start compression
    jpeg_start_compress(&cinfo, TRUE);
    
    // Allocate memory for one row of image data in RGB format
    JSAMPROW row_pointer[1];
    row_pointer[0] = new JSAMPLE[cinfo.image_width * 3];
    
    // Process each row
    while (cinfo.next_scanline < cinfo.image_height) {
        // Convert row from BGRA/RGBA to RGB format
        for (int x = 0; x < cinfo.image_width; x++) {
            unsigned long pixel = XGetPixel(image, x, cinfo.next_scanline);
            
            // X11 returns pixel values in platform's native format, need to extract RGB
            unsigned char r = (pixel >> 16) & 0xFF;
            unsigned char g = (pixel >> 8) & 0xFF;
            unsigned char b = pixel & 0xFF;
            
            row_pointer[0][x * 3 + 0] = r;
            row_pointer[0][x * 3 + 1] = g;
            row_pointer[0][x * 3 + 2] = b;
        }
        
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    
    // Finish compression
    jpeg_finish_compress(&cinfo);
    
    // Clean up
    delete[] row_pointer[0];
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);
    
    std::cout << "✅ Image saved as JPEG: " << filename << std::endl;
    return true;
}

// Function to list recent captures
std::vector<CaptureInfo> list_recent_captures(const std::string& directory = "captures") {
    std::vector<CaptureInfo> files;
    
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        std::cerr << "❌ Capture directory does not exist: " << directory << std::endl;
        return files;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                CaptureInfo info;
                info.filename = entry.path().filename().string();
                info.file_time = entry.last_write_time();
                
                // Parse information from filename
                std::string name = info.filename;
                
                // Extract type (window/screen)
                if (name.find("window_") != std::string::npos) {
                    info.type = "window";
                } else if (name.find("screen_") != std::string::npos) {
                    info.type = "screen";
                } else {
                    info.type = "other";
                }
                
                // Extract timestamp
                size_t date_pos = name.find_last_of("_");
                size_t ext_pos = name.find_last_of(".");
                if (date_pos != std::string::npos && ext_pos != std::string::npos && date_pos < ext_pos) {
                    std::string date_part = name.substr(date_pos + 1, ext_pos - date_pos - 1);
                    // Format nicely for display
                    if (date_part.length() >= 14) { // YYYYMMDD_HHMMSS format
                        info.timestamp = date_part.substr(0, 8) + " " + 
                                        date_part.substr(9, 2) + ":" + 
                                        date_part.substr(11, 2) + ":" + 
                                        date_part.substr(13, 2);
                    } else {
                        info.timestamp = date_part;
                    }
                }
                
                files.push_back(info);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "❌ Error listing capture directory: " << e.what() << std::endl;
    }
    
    // Sort by last modified time (newest first)
    std::sort(files.begin(), files.end(), 
             [](const CaptureInfo& a, const CaptureInfo& b) {
                 return a.file_time > b.file_time;
             });
    
    return files;
}

// --- Recent Captures UI Components ---
class RecentCapturesPanel {
public:
    RecentCapturesPanel(MyWindow* parent_window) : parent_window_(parent_window) {
        // Create UI components
        box_.set_orientation(Gtk::ORIENTATION_VERTICAL);
        
        Gtk::Label* header_label = Gtk::manage(new Gtk::Label("Recent Captures:"));
        header_label->set_xalign(0);
        header_label->set_margin_bottom(5);
        header_label->set_margin_top(10);
        
        files_box_.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
        files_box_.set_homogeneous(true);
        files_box_.set_spacing(10);
        
        scrolled_window_.add(files_box_);
        scrolled_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_NEVER);
        scrolled_window_.set_min_content_height(150);
        
        box_.pack_start(*header_label, false, false);
        box_.pack_start(scrolled_window_, true, true);
        
        // Create Open Folder button
        Gtk::Button* open_folder_button = Gtk::manage(new Gtk::Button("Open Captures Folder"));
        open_folder_button->signal_clicked().connect(
            sigc::mem_fun(*this, &RecentCapturesPanel::on_open_folder_clicked));
            
        Gtk::Box* button_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
        button_box->pack_start(*open_folder_button, false, false);
        button_box->set_margin_top(5);
        
        box_.pack_start(*button_box, false, false);
        
        // Load initial captures
        refresh();
    }
    
    Gtk::Box& get_box() {
        return box_;
    }
    
    void refresh() {
        // Clear existing items
        for (auto& widget : thumbnail_widgets_) {
            files_box_.remove(*widget);
            delete widget;
        }
        thumbnail_widgets_.clear();
        capture_files_.clear();
        
        // Ensure captures directory exists
        ensure_captures_directory();
        
        // Get list of capture files
        capture_files_ = list_capture_files();
        
        // Sort files by modification time (newest first)
        std::sort(capture_files_.begin(), capture_files_.end(), 
            [](const std::string& a, const std::string& b) {
                struct stat stat_a, stat_b;
                stat(a.c_str(), &stat_a);
                stat(b.c_str(), &stat_b);
                return stat_a.st_mtime > stat_b.st_mtime;
            });
        
        // Limit to the most recent 10 files
        size_t max_files = 10;
        if (capture_files_.size() > max_files) {
            capture_files_.resize(max_files);
        }
        
        // Add each thumbnail
        for (const std::string& file_path : capture_files_) {
            add_thumbnail(file_path);
        }
        
        files_box_.show_all();
    }

private:
    void add_thumbnail(const std::string& file_path) {
        // Extract filename from path
        size_t slash_pos = file_path.find_last_of('/');
        std::string filename = (slash_pos != std::string::npos) ? 
            file_path.substr(slash_pos + 1) : file_path;
        
        // Create a new thumbnail widget
        Gtk::Box* thumbnail_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
        thumbnail_box->set_border_width(5);
        
        // Create image widget
        Gtk::Image* image = Gtk::manage(new Gtk::Image());
        try {
            Glib::RefPtr<Gdk::Pixbuf> pixbuf = Gdk::Pixbuf::create_from_file(file_path, 120, 90, true);
            image->set(pixbuf);
        } catch (const Glib::Exception& ex) {
            std::cerr << "Error loading image: " << ex.what() << std::endl;
            image->set_from_icon_name("image-missing", Gtk::ICON_SIZE_DIALOG);
        }
        
        // Create a button with the image
        Gtk::Button* image_button = Gtk::manage(new Gtk::Button());
        image_button->set_image(*image);
        image_button->set_tooltip_text(file_path);
        image_button->signal_clicked().connect(
            sigc::bind(sigc::mem_fun(*this, &RecentCapturesPanel::on_thumbnail_clicked), file_path));
        
        // Create label with filename
        Gtk::Label* name_label = Gtk::manage(new Gtk::Label(filename));
        name_label->set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
        name_label->set_max_width_chars(15);
        
        // Add to thumbnail box
        thumbnail_box->pack_start(*image_button, false, false);
        thumbnail_box->pack_start(*name_label, false, false);
        
        // Add to container
        files_box_.pack_start(*thumbnail_box, false, false);
        thumbnail_widgets_.push_back(thumbnail_box);
    }
    
    void on_thumbnail_clicked(std::string file_path) {
        // Open the image in the default viewer
        std::string command = "xdg-open \"" + file_path + "\" &";
        system(command.c_str());
    }
    
    void on_open_folder_clicked() {
        system("xdg-open captures &");
    }
    
    std::vector<std::string> list_capture_files() {
        std::vector<std::string> files;
        DIR* dir = opendir("captures");
        if (dir != nullptr) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string filename = entry->d_name;
                if (filename == "." || filename == "..") {
                    continue;
                }
                
                // Check if it's an image file
                if (filename.rfind(".png") != std::string::npos ||
                    filename.rfind(".jpg") != std::string::npos ||
                    filename.rfind(".jpeg") != std::string::npos) {
                    files.push_back("captures/" + filename);
                }
            }
            closedir(dir);
        }
        return files;
    }
    
    Gtk::Box box_;
    Gtk::ScrolledWindow scrolled_window_;
    Gtk::Box files_box_;
    std::vector<Gtk::Widget*> thumbnail_widgets_;
    std::vector<std::string> capture_files_;
    MyWindow* parent_window_;
};

// --- MyWindow Class Definition ---
class MyWindow : public Gtk::Window {
public:
    MyWindow() : window_capturer(), box(Gtk::ORIENTATION_VERTICAL, 5), recent_captures_panel(this) {
        set_title("PipeWrench - Window Capture Tool");
        set_default_size(700, 500);
        set_border_width(10);
        
        // Create the top label
        Gtk::Label* header_label = Gtk::manage(new Gtk::Label("Select a window or screen to capture:"));
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
        tree_view.append_column("ID", columns.col_id);
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
        
        // Create source selection
        Gtk::Box* source_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
        Gtk::Label* source_label = Gtk::manage(new Gtk::Label("Source Type:"));
        source_combo.append("window", "Windows");
        source_combo.append("screen", "Screens");
        source_combo.append("all", "All Sources");
        source_combo.set_active_id("window");
        source_combo.signal_changed().connect(
            sigc::mem_fun(*this, &MyWindow::on_source_changed));
        source_box->pack_start(*source_label, false, false);
        source_box->pack_start(source_combo, false, false);
        
        // Create checkbox for window decorations
        decorations_check.set_label("Include window decorations");
        decorations_check.set_active(false);
        source_box->pack_start(decorations_check, false, false);
        
        // Create debug checkbox
        debug_check.set_label("Show Diagnostic Info");
        debug_check.set_active(false);
        debug_check.signal_toggled().connect(
            sigc::mem_fun(*this, &MyWindow::on_debug_toggled));
        source_box->pack_start(debug_check, false, false);
        
        // Create action buttons
        refresh_button.set_label("Refresh List");
        capture_button.set_label("Capture Selected");
        capture_button.set_sensitive(false); // Disabled until a window is selected
        
        // Create format selection
        format_label.set_text("Format:");
        format_combo.append("png", "PNG");
        format_combo.append("jpg", "JPEG");
        format_combo.set_active_id("png");
        
        // Create button box for controls
        Gtk::Box* controls_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
        controls_box->pack_start(refresh_button, false, false);
        controls_box->pack_start(capture_button, false, false);
        controls_box->pack_end(format_combo, false, false);
        controls_box->pack_end(format_label, false, false);
        
        // Create status bar
        status_bar.push("Ready. No item selected.");
        
        // Setup debug text view
        debug_window.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        debug_window.add(debug_view);
        debug_window.set_min_content_height(100);
        debug_buffer = debug_view.get_buffer();
        debug_view.set_editable(false);
        debug_view.set_monospace(true);
        debug_window.set_no_show_all(true); // Hidden by default
        
        // Pack all widgets into the main box
        box.pack_start(*header_label, false, false);
        box.pack_start(*source_box, false, false);
        box.pack_start(scrolled_window, true, true);
        box.pack_start(*controls_box, false, false);
        box.pack_start(debug_window, true, true);
        box.pack_start(recent_captures_panel.get_box(), true, true);
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
        populate_list();
        
        // Redirect cout to our debug window
        cout_buffer = std::cout.rdbuf();
        debug_streambuf = new DebugStreambuf(this);
        std::cout.rdbuf(debug_streambuf);
        
        std::cout << "  MyWindow constructor finished." << std::endl;
    }
    
    virtual ~MyWindow() {
        // Restore stdout
        std::cout.rdbuf(cout_buffer);
        delete debug_streambuf;
        
        std::cout << "  MyWindow destructor called." << std::endl;
    }

protected:
    // Signal handlers
    void on_refresh_clicked() {
        populate_list();
        recent_captures_panel.refresh();
        status_bar.push("List refreshed.");
    }
    
    void on_source_changed() {
        populate_list();
        status_bar.push("Source type changed to: " + source_combo.get_active_text());
    }
    
    void on_debug_toggled() {
        if (debug_check.get_active()) {
            debug_window.show();
        } else {
            debug_window.hide();
        }
    }
    
    void on_decorations_toggled() {
        populate_list();
        status_bar.push("Window decoration preference updated.");
    }
    
    void on_capture_clicked() {
        Glib::RefPtr<Gtk::TreeSelection> selection = tree_view.get_selection();
        Gtk::TreeModel::iterator iter = selection->get_selected();
        
        if (!iter) {
            status_bar.push("No item selected for capture.");
            return;
        }
        
        // Get the selected row
        Gtk::TreeModel::Row row = *iter;
        Glib::ustring item_type_ustr = row[columns.col_item_type];
        std::string item_type = item_type_ustr.raw();
        
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
        
        if (item_type == "window") {
            // Handle window capture
            X11ScreenCapturer::WindowInfo* window_info = row[columns.col_window_info_ptr];
            if (!window_info) {
                status_bar.push("Invalid window information.");
                return;
            }
            
            std::string filename = "captures/window_" + std::string(timestamp) + "." + format;
            status_bar.push("Capturing window: " + window_info->title);
            
            if (format == "jpg") {
                if (save_image_as_jpeg(window_capturer.capture_window_image(*window_info), filename)) {
                    status_bar.push("Window captured successfully: " + filename);
                    recent_captures_panel.refresh();
                } else {
                    status_bar.push("Failed to capture window.");
                }
            } else {
                if (window_capturer.capture_window(*window_info, filename)) {
                    status_bar.push("Window captured successfully: " + filename);
                    recent_captures_panel.refresh();
                } else {
                    status_bar.push("Failed to capture window.");
                }
            }
        } else if (item_type == "screen") {
            // Handle screen capture
            X11ScreenCapturer::ScreenInfo* screen_info = row[columns.col_screen_info_ptr];
            if (!screen_info) {
                status_bar.push("Invalid screen information.");
                return;
            }
            
            std::string filename = "captures/screen_" + std::string(timestamp) + "." + format;
            status_bar.push("Capturing screen: " + screen_info->name);
            
            if (format == "jpg") {
                if (save_image_as_jpeg(window_capturer.capture_screen_image(screen_info->number), filename)) {
                    status_bar.push("Screen captured successfully: " + filename);
                    recent_captures_panel.refresh();
                } else {
                    status_bar.push("Failed to capture screen.");
                }
            } else {
                if (window_capturer.capture_screen(screen_info->number, filename)) {
                    status_bar.push("Screen captured successfully: " + filename);
                    recent_captures_panel.refresh();
                } else {
                    status_bar.push("Failed to capture screen.");
                }
            }
        }
    }
    
    void on_selection_changed() {
        Glib::RefPtr<Gtk::TreeSelection> selection = tree_view.get_selection();
        Gtk::TreeModel::iterator iter = selection->get_selected();
        
        if (iter) {
            Gtk::TreeModel::Row row = *iter;
            Glib::ustring item_title = row[columns.col_title];
            Glib::ustring item_type = row[columns.col_item_type];
            status_bar.push("Selected: " + item_title + " (" + item_type + ")");
            capture_button.set_sensitive(true);
        } else {
            status_bar.push("No item selected.");
            capture_button.set_sensitive(false);
        }
    }
    
    void on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column) {
        // Handle double-click (same as clicking capture button)
        on_capture_clicked();
    }
    
    // Handle keyboard shortcuts
    bool on_key_press_event(GdkEventKey* key_event) override {
        // Check for Ctrl+R (Refresh)
        if ((key_event->state & GDK_CONTROL_MASK) && key_event->keyval == GDK_KEY_r) {
            on_refresh_clicked();
            return true;
        }
        
        // Check for Ctrl+C (Capture selected)
        if ((key_event->state & GDK_CONTROL_MASK) && key_event->keyval == GDK_KEY_c) {
            on_capture_clicked();
            return true;
        }
        
        // Check for Ctrl+O (Open captures folder)
        if ((key_event->state & GDK_CONTROL_MASK) && key_event->keyval == GDK_KEY_o) {
            system("xdg-open captures");
            return true;
        }
        
        // Check for F5 (Refresh)
        if (key_event->keyval == GDK_KEY_F5) {
            on_refresh_clicked();
            return true;
        }
        
        // Check for F12 (Toggle debug window)
        if (key_event->keyval == GDK_KEY_F12) {
            debug_check.set_active(!debug_check.get_active());
            return true;
        }
        
        // Let parent class handle other keys
        return Gtk::Window::on_key_press_event(key_event);
    }
    
    // Populate the list with windows or screens based on current selection
    void populate_list() {
        list_store->clear();
        window_infos.clear();
        screen_infos.clear();
        
        std::string source_type = source_combo.get_active_id();
        
        if (source_type == "window" || source_type == "all") {
            // Add windows to the list
            std::vector<X11ScreenCapturer::WindowInfo> windows = window_capturer.list_windows();
            window_infos = windows;
            
            // Add detailed info to debug
            std::cout << "🔍 Window Detection Details:" << std::endl;
            for (size_t i = 0; i < window_infos.size(); i++) {
                X11ScreenCapturer::WindowInfo& info = window_infos[i];
                std::cout << "  Window " << i << ":" << std::endl;
                std::cout << "    ID: " << info.id << std::endl;
                std::cout << "    Title: \"" << info.title << "\"" << std::endl;
                std::cout << "    Position: (" << info.x << "," << info.y << ")" << std::endl;
                std::cout << "    Size: " << info.width << "×" << info.height << std::endl;
                std::cout << "    Visible: " << (info.is_visible ? "Yes" : "No") << std::endl;
                
                Gtk::TreeModel::Row row = *(list_store->append());
                row[columns.col_id] = Glib::ustring::format(info.id);
                row[columns.col_title] = info.title;
                row[columns.col_window_type] = "Window";
                row[columns.col_dimensions] = Glib::ustring::format(info.width, "×", info.height);
                row[columns.col_position] = Glib::ustring::format("(", info.x, ",", info.y, ")");
                row[columns.col_window_info_ptr] = &info;
                row[columns.col_screen_info_ptr] = nullptr;
                row[columns.col_item_type] = "window";
            }
        }
        
        if (source_type == "screen" || source_type == "all") {
            // Add screens to the list
            std::vector<X11ScreenCapturer::ScreenInfo> screens = window_capturer.detect_screens();
            screen_infos = screens;
            
            for (size_t i = 0; i < screen_infos.size(); i++) {
                X11ScreenCapturer::ScreenInfo& info = screen_infos[i];
                
                Gtk::TreeModel::Row row = *(list_store->append());
                row[columns.col_id] = info.number < 0 ? "ALL" : Glib::ustring::format(info.number);
                row[columns.col_title] = info.name;
                row[columns.col_window_type] = "Screen";
                row[columns.col_dimensions] = Glib::ustring::format(info.width, "×", info.height);
                row[columns.col_position] = Glib::ustring::format("(", info.x, ",", info.y, ")");
                row[columns.col_window_info_ptr] = nullptr;
                row[columns.col_screen_info_ptr] = &info;
                row[columns.col_item_type] = "screen";
            }
        }
        
        std::cout << "  Added " << list_store->children().size() << " items to the list." << std::endl;
    }
    
    bool on_delete_event(GdkEventAny* event) override {
        std::cout << "  MyWindow delete event." << std::endl;
        return Gtk::Window::on_delete_event(event);
    }
    
    // Add text to debug window
    void add_debug_text(const std::string& text) {
        Glib::RefPtr<Gtk::TextBuffer> buffer = debug_view.get_buffer();
        buffer->insert(buffer->end(), text);
        
        // Scroll to the end
        auto mark = buffer->create_mark("end", buffer->end(), false);
        debug_view.scroll_to(mark);
        buffer->delete_mark(mark);
    }

private:
    // Debug streambuf to redirect cout to our debug window
    class DebugStreambuf : public std::streambuf {
    public:
        DebugStreambuf(MyWindow* window) : window_(window) {}
        
    protected:
        virtual int_type overflow(int_type c = traits_type::eof()) {
            if (c != traits_type::eof()) {
                buffer_ += static_cast<char>(c);
                if (c == '\n') {
                    // Add to debug window on newline
                    window_->add_debug_text(buffer_);
                    buffer_.clear();
                }
            }
            return c;
        }
        
    private:
        MyWindow* window_;
        std::string buffer_;
    };
    
    // UI components
    Gtk::Box box;
    Gtk::ScrolledWindow scrolled_window;
    Gtk::TreeView tree_view;
    Glib::RefPtr<Gtk::ListStore> list_store;
    Gtk::Button refresh_button;
    Gtk::Button capture_button;
    Gtk::CheckButton decorations_check;
    Gtk::CheckButton debug_check;
    Gtk::Label format_label;
    Gtk::ComboBoxText format_combo;
    Gtk::ComboBoxText source_combo;
    Gtk::Statusbar status_bar;
    
    // Debug components
    Gtk::ScrolledWindow debug_window;
    Gtk::TextView debug_view;
    Glib::RefPtr<Gtk::TextBuffer> debug_buffer;
    DebugStreambuf* debug_streambuf;
    std::streambuf* cout_buffer;
    
    // Capture functionality
    X11ScreenCapturer window_capturer;
    std::vector<X11ScreenCapturer::WindowInfo> window_infos;
    std::vector<X11ScreenCapturer::ScreenInfo> screen_infos;
    
    // Recent captures panel
    RecentCapturesPanel recent_captures_panel;
    
    // Window columns class
    class WindowColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        WindowColumns() {
            add(col_id);
            add(col_title);
            add(col_window_type);
            add(col_dimensions);
            add(col_position);
            add(col_window_info_ptr);
            add(col_screen_info_ptr);
            add(col_item_type);
        }

        Gtk::TreeModelColumn<Glib::ustring> col_id;
        Gtk::TreeModelColumn<Glib::ustring> col_title;
        Gtk::TreeModelColumn<Glib::ustring> col_window_type;
        Gtk::TreeModelColumn<Glib::ustring> col_dimensions;
        Gtk::TreeModelColumn<Glib::ustring> col_position;
        Gtk::TreeModelColumn<X11ScreenCapturer::WindowInfo*> col_window_info_ptr;
        Gtk::TreeModelColumn<X11ScreenCapturer::ScreenInfo*> col_screen_info_ptr;
        Gtk::TreeModelColumn<Glib::ustring> col_item_type;
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

