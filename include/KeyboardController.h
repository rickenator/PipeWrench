#ifndef KEYBOARD_CONTROLLER_H
#define KEYBOARD_CONTROLLER_H

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <glibmm/main.h>
#include <sigc++/signal.h>
#include <sigc++/connection.h>
#include <map>
#include <vector>
#include <string>

/**
 * Class for handling global keyboard shortcuts in X11 environment.
 * Currently supports intercepting the Numpad Enter key to trigger captures.
 */
class KeyboardController {
public:
    /**
     * Constructor initializes the X11 connection.
     */
    KeyboardController();
    
    /**
     * Destructor cleans up X11 resources and releases grabbed keys.
     */
    ~KeyboardController();
    
    /**
     * Start monitoring for keyboard shortcuts.
     * @return True if monitoring was successfully started.
     */
    bool start_monitoring();
    
    /**
     * Stop monitoring keyboard shortcuts.
     */
    void stop_monitoring();
    
    /**
     * Check if monitoring is active.
     * @return True if monitoring is active.
     */
    bool is_monitoring() const;
    
    /**
     * Signal emitted when Numpad Enter is pressed.
     * This can be connected to trigger capture functionality.
     */
    typedef sigc::signal<void> type_signal_capture_key_pressed;
    type_signal_capture_key_pressed signal_capture_key_pressed() { return m_signal_capture_key_pressed; }
    
private:
    // X11 resources
    Display* display_;
    Window root_window_;
    bool monitoring_;
    sigc::connection event_check_connection_;
    
    // Key grabbing
    int numpad_enter_keycode_;
    std::vector<int> grabbed_modifiers_;
    
    // Signals
    type_signal_capture_key_pressed m_signal_capture_key_pressed;
    
    // X11 event processing
    bool process_x11_events();
    void register_keyboard_shortcuts();
    void unregister_keyboard_shortcuts();
    
    // Error handling for X11
    static int x11_error_handler(Display*, XErrorEvent*);
};

#endif // KEYBOARD_CONTROLLER_H
