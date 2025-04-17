#ifndef RECENT_CAPTURES_PANEL_H
#define RECENT_CAPTURES_PANEL_H

#include <gtkmm.h>
#include <vector>
#include <string>
#include <filesystem>

class RecentCapturesPanel : public Gtk::Box {
public:
    RecentCapturesPanel();
    virtual ~RecentCapturesPanel();
    
    void add_capture(const std::string& filename);
    void clear_captures();
    void load_captures();
    
    // Signal accessors
    typedef sigc::signal<void, const std::string&> type_signal_thumbnail_activated;
    type_signal_thumbnail_activated signal_thumbnail_activated();
    
private:
    // UI components
    Gtk::Label title_label_;
    Gtk::ScrolledWindow scrolled_window_;
    Gtk::FlowBox flowbox_;
    
    std::vector<std::string> capture_filenames_;
    int thumbnail_size_;
    
    // Signals
    type_signal_thumbnail_activated m_signal_thumbnail_activated;

    // Helper methods
    void setup_ui();
    void add_thumbnail(const std::filesystem::path& filepath, bool at_beginning = false);
    void on_thumbnail_activated(Gtk::FlowBoxChild* child);
    void open_image(const std::string& filepath);
};

#endif // RECENT_CAPTURES_PANEL_H