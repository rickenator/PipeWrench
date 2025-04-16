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

// X11-based screen capture functionality with XCopyArea approach
class X11ScreenCapturer {
public:
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
    
    bool capture_screen() {
        if (!display_) {
            std::cerr << "❌ X11 display not initialized" << std::endl;
            return false;
        }
        
        // Create a small test capture area
        int capture_width = 200;
        int capture_height = 200;
        
        std::cout << "🔍 Attempting to capture area: " << capture_width << "x" << capture_height << std::endl;
        
        try {
            // Create a pixmap to draw into
            int depth = DefaultDepth(display_, DefaultScreen(display_));
            pixmap_ = XCreatePixmap(display_, root_, capture_width, capture_height, depth);
            
            // Create a graphics context
            GC gc = XCreateGC(display_, pixmap_, 0, nullptr);
            
            // Copy the screen region to the pixmap
            XCopyArea(display_, root_, pixmap_, gc, 0, 0, capture_width, capture_height, 0, 0);
            
            // Get the image from the pixmap (this is safer than direct screen capture)
            XImage* image = XGetImage(display_, pixmap_, 0, 0, capture_width, capture_height, AllPlanes, ZPixmap);
            
            if (!image) {
                std::cerr << "❌ Failed to capture screen image from pixmap" << std::endl;
                XFreeGC(display_, gc);
                return false;
            }
            
            std::cout << "📸 Screen area captured successfully via pixmap: " << capture_width << "x" << capture_height 
                      << " with depth " << image->depth << std::endl;
            
            // Here you would process the image data
            // For now, we just free it
            XDestroyImage(image);
            XFreeGC(display_, gc);
            
            return true;
        } catch (...) {
            std::cerr << "❌ Exception during X11 screen capture" << std::endl;
            return false;
        }
    }
    
private:
    Display* display_;
    int width_;
    int height_;
    Window root_;
    Pixmap pixmap_;
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
    return x11_capturer_.capture_screen();
}

// --- MyWindow Class Definition (Needs to be before main) ---
class MyWindow : public Gtk::Window {
public:
    MyWindow() {
        set_title("PipeWrench");
        set_default_size(200, 100);
        Gtk::Label* label = Gtk::manage(new Gtk::Label("Requesting Screen Capture..."));
        add(*label);
        show_all_children();
        std::cout << "  MyWindow constructor finished." << std::endl; // Added log
    }
    virtual ~MyWindow() {
        std::cout << "  MyWindow destructor called." << std::endl; // Added log
    }

protected:
    // Optional: Override signal handlers if needed, e.g., for closing
    bool on_delete_event(GdkEventAny* event) override {
        std::cout << "  MyWindow delete event." << std::endl; // Added log
        // Return false to allow window closure, true to prevent it
        return Gtk::Window::on_delete_event(event);
    }
};

// --- Global pointer for the main window (Needs to be before main) ---
MyWindow* main_window = nullptr;


// --- Main Function ---
int main(int argc, char* argv[]) {
    // Test X11 capture directly before initializing GTK application
    std::cout << "🧪 Testing direct X11 capture before GTK initialization..." << std::endl;
    X11ScreenCapturer test_capturer;
    bool test_result = test_capturer.capture_screen();
    std::cout << "🧪 Direct X11 capture test result: " << (test_result ? "SUCCESS" : "FAILURE") << std::endl;
    
    std::cout << "🔧 Initializing Gtk::Application..." << std::endl;
    auto app = Gtk::Application::create(argc, argv, "org.example.PipeWrench");
    std::cout << "✅ Gtk::Application created." << std::endl;

    app->signal_activate().connect([&]() {
        std::cout << "▶️ Gtk::Application activate signal received." << std::endl;
        if (!main_window) {
             std::cout << "  Creating MyWindow..." << std::endl;
             main_window = new MyWindow();
             app->add_window(*main_window);
             std::cout << "  MyWindow created and added to application." << std::endl;
        }
        std::cout << "  Presenting MyWindow..." << std::endl;
        main_window->present();

        // Delay the D-Bus call slightly after window presentation
        Glib::signal_timeout().connect_once([&]() {
            std::cout << "  ⏳ Timeout finished, proceeding with capture..." << std::endl;

            // Ensure window is realized *inside* the timeout callback
            if (main_window && main_window->get_window()) {
                 main_window->get_window()->ensure_native();
                 std::cout << "  MyWindow realized inside timeout." << std::endl;
            } else {
                 std::cerr << "  ⚠️ Window not available in timeout callback." << std::endl;
                 return; // Don't proceed if window is gone
            }

            // --- PipeWrench logic with fallback to direct X11 capture ---
            auto pw = std::make_shared<PipeWrench>();
            
            // First try portal method, but if it fails, use direct X11 capture
            try {
                // Try portal method first (same as before)
                
                // Get Parent Window ID
                Glib::ustring parent_window_id = "";
                auto gdk_window = main_window->get_window();
                if (gdk_window) {
                    GdkDisplay* display = gdk_window->get_display()->gobj();
                    #ifdef GDK_WINDOWING_X11
                    if (GDK_IS_X11_DISPLAY(display)) {
                        Window xid = gdk_x11_window_get_xid(gdk_window->gobj());
                        parent_window_id = Glib::ustring::compose("x11:%1", xid);
                        std::cout << "  Identified X11 window ID: " << parent_window_id << std::endl;
                    }
                    #endif
                    #ifdef GDK_WINDOWING_WAYLAND
                    if (GDK_IS_WAYLAND_DISPLAY(display)) {
                         std::cout << "  Identified Wayland display, using empty parent_window ID." << std::endl;
                    }
                    #endif
                } else {
                     std::cerr << "  ⚠️ Could not get Gdk::Window handle inside timeout." << std::endl;
                }
                
                // Add fallback to direct X11 capture if portal method fails
                Glib::signal_timeout().connect_once([pw]() {
                    std::cout << "  🔄 Trying direct X11 capture fallback method..." << std::endl;
                    bool capture_result = pw->try_direct_capture();
                    if (capture_result) {
                        std::cout << "  ✅ Direct X11 capture succeeded!" << std::endl;
                    } else {
                        std::cerr << "  ❌ Direct X11 capture failed!" << std::endl;
                    }
                }, 3000);  // Try direct capture after 3 seconds (gives portal method time to fail)
                
                // Original portal call code
                pw->session_handle_token_ = generate_token();
                std::cout << "  📤 Calling CreateSession with token: " << pw->session_handle_token_ << std::endl;

                GVariantBuilder builder;
                g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
                
                // Add only essential parameters
                g_variant_builder_add(&builder, "{sv}", "handle_token", 
                                     g_variant_new_string(pw->session_handle_token_.c_str()));
                g_variant_builder_add(&builder, "{sv}", "session_handle_token", 
                                     g_variant_new_string(pw->session_handle_token_.c_str()));
                g_variant_builder_add(&builder, "{sv}", "parent_window", 
                                     g_variant_new_string(parent_window_id.c_str()));

                // Create minimal type selection - just monitor
                GVariantBuilder types_builder;
                g_variant_builder_init(&types_builder, G_VARIANT_TYPE("au"));
                g_variant_builder_add(&types_builder, "u", 1);  // Monitor only
                g_variant_builder_add(&builder, "{sv}", "types", 
                                     g_variant_builder_end(&types_builder));

                auto options = Glib::wrap(g_variant_builder_end(&builder));
                std::cout << "  🧪 Options: " << options.print() << std::endl;
                auto parameters = Glib::VariantContainerBase::create_tuple({ options });

                // Create cancelable to allow timeout
                auto cancellable = Gio::Cancellable::create();
                
                // Set up timeout to cancel if it takes too long
                Glib::signal_timeout().connect_once([cancellable]() {
                    std::cout << "  ⏰ Request timeout reached, cancelling..." << std::endl;
                    cancellable->cancel();
                }, 5000);  // 5 second timeout

                // Make the call with cancellable
                std::cout << "  🔍 Calling 'CreateSession' with token: " << pw->session_handle_token_ << std::endl;
                std::cout << "  🔍 Full parameters: " << parameters.print() << std::endl;
                
                pw->portal_proxy_->call(
                    "CreateSession",
                    parameters,
                    [pw](const Glib::RefPtr<Gio::AsyncResult>& result) {
                        std::cout << "  🔔 CreateSession callback received" << std::endl;
                        try {
                            auto reply = pw->portal_proxy_->call_finish(result);
                            std::cout << "  ✅ CreateSession succeeded!" << std::endl;
                            std::cout << "  🔍 Reply format: " << reply.get_type_string() << std::endl;
                            std::cout << "  🔍 Reply contents: " << reply.print() << std::endl;
                            pw->handle_create_session_response(result);
                        } catch (const Glib::Error& ex) {
                            std::cerr << "  ❌ CreateSession error: " << ex.what() << std::endl;
                            
                            // Print more detailed error info
                            std::cerr << "  ❌ Error domain: " << ex.domain() << std::endl;
                            std::cerr << "  ❌ Error code: " << ex.code() << std::endl;
                            
                            // Check if this is a D-Bus timeout error specifically
                            if (ex.code() == 24) {  // GIO_ERROR_TIMED_OUT
                                std::cerr << "  ⏱️ D-Bus timeout detected - portal service might be unresponsive" << std::endl;
                            }
                            
                            // Print suggestions to help diagnose portal issues
                            std::cout << "\n💡 Troubleshooting portal issues:" << std::endl;
                            std::cout << "  1. Check if the portal service is running: systemctl --user status xdg-desktop-portal" << std::endl;
                            std::cout << "  2. Check if the portal backend is installed: xdg-desktop-portal-gtk or xdg-desktop-portal-gnome" << std::endl;
                            std::cout << "  3. Check portal logs: journalctl --user -xe | grep desktop-portal" << std::endl;
                            std::cout << "  4. Try portal test tools: flatpak install org.freedesktop.Portal.Test" << std::endl;
                        }
                    },
                    cancellable, {}, -1);  // Fixed parameters: cancellable, empty FDList, default timeout
                std::cout << "  ✅ portal_proxy_->call() invoked." << std::endl;

            } catch (const Glib::Error& e) {
                std::cerr << "  💥 Error during setup: " << e.what() << std::endl;
                // Fallback to direct capture on error
                pw->try_direct_capture();
            }

        }, 300); // Increased delay to 300ms to ensure window is fully ready

        std::cout << "  🏁 Activate handler finished (timeout scheduled)." << std::endl;
    });

    std::cout << "🚀 Running application event loop (app->run())..." << std::endl;
    int status = app->run();
    std::cout << "🏁 Application event loop finished. Exit status: " << status << std::endl;

    return status;
}

