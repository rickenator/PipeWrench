#include "../include/RecentCapturesPanel.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <glibmm/main.h>
#include <giomm/file.h>
#include <gdkmm/pixbuf.h>

namespace fs = std::filesystem;

// Helper function to convert filesystem timestamps to time_t in a C++17 compatible way
time_t to_time_t(const fs::file_time_type& tp) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sctp);
}

RecentCapturesPanel::RecentCapturesPanel()
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 10),
      title_label_("Recent Captures"),
      thumbnail_size_(150)
{
    setup_ui();
    load_captures();
}

RecentCapturesPanel::~RecentCapturesPanel()
{
    // Clean up any resources
}

void RecentCapturesPanel::setup_ui()
{
    set_margin_top(10);
    set_margin_bottom(10);
    set_margin_start(10);
    set_margin_end(10);
    
    // Title
    title_label_.set_markup("<b>Recent Captures</b>");
    title_label_.set_halign(Gtk::ALIGN_START);
    pack_start(title_label_, Gtk::PACK_SHRINK);
    
    // Scrolled window for the FlowBox
    scrolled_window_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scrolled_window_.set_vexpand(true);
    
    // FlowBox for thumbnails
    flowbox_.set_valign(Gtk::ALIGN_START);
    flowbox_.set_max_children_per_line(4);
    flowbox_.set_selection_mode(Gtk::SELECTION_SINGLE);
    flowbox_.set_homogeneous(true);
    flowbox_.set_column_spacing(10);
    flowbox_.set_row_spacing(10);
    flowbox_.signal_child_activated().connect(
        sigc::mem_fun(*this, &RecentCapturesPanel::on_thumbnail_activated));
    
    scrolled_window_.add(flowbox_);
    pack_start(scrolled_window_, Gtk::PACK_EXPAND_WIDGET);
}

void RecentCapturesPanel::load_captures()
{
    std::cout << "Loading captures..." << std::endl;
    
    // Remove all existing children
    auto children = flowbox_.get_children();
    for (auto child : children) {
        flowbox_.remove(*child);
    }
    
    // Get all image files in the captures directory
    fs::path captures_dir = "captures";
    if (!fs::exists(captures_dir)) {
        fs::create_directory(captures_dir);
        return; // No captures yet
    }
    
    // Vector to store file info tuples (path, modification time)
    std::vector<std::pair<fs::path, time_t>> capture_files;
    
    // Collect image files
    for (const auto& entry : fs::directory_iterator(captures_dir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                // Get file's modification time
                fs::file_time_type file_time = fs::last_write_time(entry.path());
                time_t mod_time = to_time_t(file_time);
                capture_files.emplace_back(entry.path(), mod_time);
            }
        }
    }
    
    // Sort by modification time (newest first)
    std::sort(capture_files.begin(), capture_files.end(), 
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
    
    // Add thumbnails (limited to the most recent ones)
    const size_t max_thumbnails = 20;
    for (size_t i = 0; i < std::min(capture_files.size(), max_thumbnails); ++i) {
        add_thumbnail(capture_files[i].first, false);
    }
    
    show_all_children();
}

void RecentCapturesPanel::clear_captures()
{
    // Remove all existing children
    auto children = flowbox_.get_children();
    for (auto child : children) {
        flowbox_.remove(*child);
    }
}

void RecentCapturesPanel::add_capture(const std::string& filepath)
{
    fs::path file_path(filepath);
    if (fs::exists(file_path)) {
        // Add the new thumbnail at the beginning
        add_thumbnail(file_path, true);
        
        // Remove excess thumbnails if there are too many
        const size_t max_thumbnails = 20;
        auto children = flowbox_.get_children();
        if (children.size() > max_thumbnails) {
            for (size_t i = max_thumbnails; i < children.size(); ++i) {
                flowbox_.remove(*children[i]);
            }
        }
        
        show_all_children();
    }
}

void RecentCapturesPanel::add_thumbnail(const fs::path& filepath, bool at_beginning)
{
    try {
        // Create the thumbnail image
        auto pixbuf = Gdk::Pixbuf::create_from_file(filepath.string(), 
                                                   thumbnail_size_, thumbnail_size_, 
                                                   true); // preserve aspect ratio
        
        auto image = Gtk::manage(new Gtk::Image(pixbuf));
        
        // Create a box to hold the image and label
        auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 5));
        box->set_homogeneous(false);
        
        // Get the filename without path
        std::string filename = filepath.filename().string();
        
        // Get the last modified time - C++17 compatible approach
        auto last_write = fs::last_write_time(filepath);
        auto time_t = to_time_t(last_write);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        
        auto date_label = Gtk::manage(new Gtk::Label(ss.str()));
        date_label->set_line_wrap(true);
        date_label->set_max_width_chars(15);
        date_label->set_tooltip_text(ss.str());
        
        auto name_label = Gtk::manage(new Gtk::Label(filename));
        name_label->set_line_wrap(true);
        name_label->set_max_width_chars(15);
        name_label->set_tooltip_text(filename);
        
        box->pack_start(*image, Gtk::PACK_SHRINK);
        box->pack_start(*name_label, Gtk::PACK_SHRINK);
        box->pack_start(*date_label, Gtk::PACK_SHRINK);
        
        // Set the file path as user data
        box->set_data("filepath", new std::string(filepath.string()));
        
        // Add to the flowbox
        box->show_all();
        
        if (at_beginning) {
            flowbox_.insert(*box, 0);
        } else {
            flowbox_.add(*box);
        }
    } catch (const Glib::Exception& e) {
        std::cerr << "Failed to create thumbnail for " << filepath << ": " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error creating thumbnail: " << e.what() << std::endl;
    }
}

RecentCapturesPanel::type_signal_thumbnail_activated RecentCapturesPanel::signal_thumbnail_activated() {
    return m_signal_thumbnail_activated;
}

void RecentCapturesPanel::on_thumbnail_activated(Gtk::FlowBoxChild* child) {
    if (!child) return;
    
    auto* button = dynamic_cast<Gtk::Button*>(child->get_child());
    if (!button) return;

    std::string filepath = button->get_name();
    m_signal_thumbnail_activated.emit(filepath);
    open_image(filepath);
}

void RecentCapturesPanel::open_image(const std::string& filepath)
{
    // Use system command to open the file with the default application
    std::string command;
#ifdef __linux__
    command = "xdg-open \"" + filepath + "\" &";
#elif __APPLE__
    command = "open \"" + filepath + "\" &";
#elif _WIN32
    command = "start \"\" \"" + filepath + "\"";
#else
    std::cerr << "Unknown platform, cannot open file" << std::endl;
    return;
#endif
    
    std::system(command.c_str());
}