#ifndef WINDOW_COLUMNS_H
#define WINDOW_COLUMNS_H

#include <gtkmm.h>
#include "X11ScreenCapturer.h"

// TreeModel column record for window data
class WindowColumns : public Gtk::TreeModel::ColumnRecord {
public:
    WindowColumns() {
        add(m_col_icon);
        add(m_col_title);
        add(m_col_id);
        add(m_col_size);
        // additional columns
        add(col_window_type);
        add(col_dimensions);
        add(col_position);
        add(col_window_info_ptr);
        add(col_item_type);
    }

    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> m_col_icon;
    Gtk::TreeModelColumn<Glib::ustring> m_col_title;
    Gtk::TreeModelColumn<unsigned long> m_col_id;
    Gtk::TreeModelColumn<Glib::ustring> m_col_size;
    // columns for SauronWindow
    Gtk::TreeModelColumn<Glib::ustring> col_window_type;
    Gtk::TreeModelColumn<Glib::ustring> col_dimensions;
    Gtk::TreeModelColumn<Glib::ustring> col_position;
    Gtk::TreeModelColumn<const X11ScreenCapturer::WindowInfo*> col_window_info_ptr;
    Gtk::TreeModelColumn<Glib::ustring> col_item_type;
};

// TreeModel column record for screen data
class ScreenColumns : public Gtk::TreeModel::ColumnRecord {
public:
    ScreenColumns() {
        add(m_col_icon);
        add(m_col_name);
        add(m_col_id);
        add(m_col_resolution);
    }

    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> m_col_icon;
    Gtk::TreeModelColumn<Glib::ustring> m_col_name;
    Gtk::TreeModelColumn<int> m_col_id;
    Gtk::TreeModelColumn<Glib::ustring> m_col_resolution;
};

#endif // WINDOW_COLUMNS_H