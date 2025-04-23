#include "../../include/KeyboardController.h"
#include <iostream>
#include <stdexcept>
#include <X11/Xutil.h>

// Static error handler variable to track if key grabbing fails
static bool had_x11_error = false;

// X11 error handler to catch errors without crashing
int KeyboardController::x11_error_handler(Display* display, XErrorEvent* error) {
    if (error->error_code == BadAccess) {
        had_x11_error = true;
        std::cerr << "⚠️ X11 Error: Another application has already grabbed this key" << std::endl;
    } else {
        char error_text[256];
        XGetErrorText(display, error->error_code, error_text, sizeof(error_text));
        std::cerr << "⚠️ X11 Error: " << error_text << std::endl;
    }
    return 0;
}

KeyboardController::KeyboardController() : 
    display_(nullptr), 
    monitoring_(false),
    numpad_enter_keycode_(0) {
    
    // Initialize X11 connection
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        std::cerr << "❌ Failed to open X display for keyboard control" << std::endl;
        return;
    }
    
    root_window_ = DefaultRootWindow(display_);
    
    // Get keycode for Numpad Enter
    numpad_enter_keycode_ = XKeysymToKeycode(display_, XK_KP_Enter);
    if (numpad_enter_keycode_ == 0) {
        std::cerr << "❌ Could not map Numpad Enter key" << std::endl;
    }
    
    // Initialize modifiers to grab (combinations with NumLock, etc.)
    // Only use the lower 3 bits of modifiers (Shift, Control, Alt)
    for (int mod = 0; mod < (1 << 3); ++mod) {
        grabbed_modifiers_.push_back(mod);
    }
    
    std::cout << "✅ Keyboard controller initialized" << std::endl;
}

KeyboardController::~KeyboardController() {
    // Stop monitoring and release resources
    stop_monitoring();
    
    // Close X display
    if (display_) {
        XCloseDisplay(display_);
        display_ = nullptr;
    }
}

bool KeyboardController::start_monitoring() {
    if (!display_) {
        std::cerr << "❌ Cannot start keyboard monitoring: No X display connection" << std::endl;
        return false;
    }
    
    if (monitoring_) {
        // Already monitoring
        return true;
    }
    
    // Set up error handler
    had_x11_error = false;
    XErrorHandler old_handler = XSetErrorHandler(&KeyboardController::x11_error_handler);
    
    // Register keyboard shortcuts
    register_keyboard_shortcuts();
    
    // Sync to ensure errors are processed
    XSync(display_, False);
    
    // Restore error handler
    XSetErrorHandler(old_handler);
    
    // Check if we had an error during key grabbing
    if (had_x11_error) {
        unregister_keyboard_shortcuts();
        std::cerr << "❌ Failed to grab keyboard shortcuts: They may be in use by another application" << std::endl;
        return false;
    }
    
    // Start a timer to check for X11 events
    event_check_connection_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &KeyboardController::process_x11_events),
        100); // Check every 100ms
    
    monitoring_ = true;
    std::cout << "✅ Keyboard shortcut monitoring started (Numpad Enter)" << std::endl;
    
    return true;
}

void KeyboardController::stop_monitoring() {
    if (!monitoring_) {
        return;
    }
    
    // Disconnect the event check timer
    if (event_check_connection_.connected()) {
        event_check_connection_.disconnect();
    }
    
    // Unregister shortcuts
    unregister_keyboard_shortcuts();
    
    monitoring_ = false;
    std::cout << "✅ Keyboard shortcut monitoring stopped" << std::endl;
}

bool KeyboardController::is_monitoring() const {
    return monitoring_;
}

void KeyboardController::register_keyboard_shortcuts() {
    if (!display_ || numpad_enter_keycode_ == 0) {
        return;
    }
    
    // Select input for key events
    XSelectInput(display_, root_window_, KeyPressMask);
    
    // Grab Numpad Enter with different modifier combinations
    for (int mod : grabbed_modifiers_) {
        XGrabKey(display_, numpad_enter_keycode_, mod, root_window_, 
                True, GrabModeAsync, GrabModeAsync);
    }
    
    // Flush to ensure events are registered
    XFlush(display_);
}

void KeyboardController::unregister_keyboard_shortcuts() {
    if (!display_ || numpad_enter_keycode_ == 0) {
        return;
    }
    
    // Ungrab Numpad Enter with all modifier combinations
    for (int mod : grabbed_modifiers_) {
        XUngrabKey(display_, numpad_enter_keycode_, mod, root_window_);
    }
    
    // Flush to ensure events are processed
    XFlush(display_);
}

bool KeyboardController::process_x11_events() {
    if (!display_ || !monitoring_) {
        return false;  // Stop this timer
    }
    
    // Check for pending X events
    while (XPending(display_)) {
        XEvent event;
        XNextEvent(display_, &event);
        
        // Handle key press events
        if (event.type == KeyPress && event.xkey.keycode == numpad_enter_keycode_) {
            std::cout << "🔑 Numpad Enter pressed (intercepted)" << std::endl;
            
            // Emit signal to trigger capture
            m_signal_capture_key_pressed.emit();
        }
    }
    
    return true;  // Continue this timer
}
