// Implementation of SauronWindow AI agent functions
#include "../../include/SauronWindow.h"
#include <string>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <gtkmm/messagedialog.h>
#include <glibmm/keyfile.h>
#include <glibmm/spawn.h>

using json = nlohmann::json;

void SauronWindow::on_start_agent_clicked() {
    // Check if MQTT is connected
    if (!mqtt_connected_) {
        Gtk::MessageDialog dialog(*this, "MQTT Not Connected", false, Gtk::MESSAGE_WARNING);
        dialog.set_secondary_text("Please connect to MQTT first to enable communication with the AI agent.");
        dialog.run();
        return;
    }
    
    // Start the SauronAgent process
    std::string agent_path = "sauron_agent";
    
    // Check if we're running from the build directory
    if (std::filesystem::exists("./build/sauron_agent")) {
        agent_path = "./build/sauron_agent";
    }
    
    try {
        Glib::spawn_command_line_async(agent_path);
        status_bar_.push("Started AI Agent process");
        add_debug_text("🤖 Started AI Agent process\n");
        
        // Disable the start button to prevent multiple instances
        start_agent_button_.set_sensitive(false);
        start_agent_button_.set_label("Agent Running");
    } catch (const Glib::Error& e) {
        Gtk::MessageDialog dialog(*this, "Failed to Start Agent", false, Gtk::MESSAGE_ERROR);
        dialog.set_secondary_text("Error: " + std::string(e.what()));
        dialog.run();
        add_debug_text("❌ Failed to start AI Agent: " + std::string(e.what()) + "\n");
    }
}

void SauronWindow::on_agent_settings_clicked() {
    // Create a dialog to configure the AI agent
    Gtk::Dialog dialog("AI Agent Settings", *this, true);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Save", Gtk::RESPONSE_OK);
    
    auto content_area = dialog.get_content_area();
    content_area->set_border_width(10);
    content_area->set_spacing(10);
    
    // Add settings fields
    Gtk::Label header_label("Configure AI Agent Backend");
    header_label.set_markup("<b>Configure AI Agent Backend</b>");
    content_area->pack_start(header_label, false, false);
    
    // Backend type selection
    Gtk::Box backend_box(Gtk::ORIENTATION_HORIZONTAL, 5);
    Gtk::Label backend_label("AI Backend:");
    Gtk::ComboBoxText backend_combo;
    backend_combo.append("openai", "OpenAI API");
    backend_combo.append("ollama", "Ollama (Local)");
    backend_combo.set_active(0);
    backend_box.pack_start(backend_label, false, false);
    backend_box.pack_start(backend_combo, true, true);
    content_area->pack_start(backend_box, false, false);
    
    // API Key
    Gtk::Box api_key_box(Gtk::ORIENTATION_HORIZONTAL, 5);
    Gtk::Label api_key_label("API Key:");
    Gtk::Entry api_key_entry;
    api_key_entry.set_visibility(false); // Hide API key
    api_key_box.pack_start(api_key_label, false, false);
    api_key_box.pack_start(api_key_entry, true, true);
    content_area->pack_start(api_key_box, false, false);
    
    // API Host
    Gtk::Box api_host_box(Gtk::ORIENTATION_HORIZONTAL, 5);
    Gtk::Label api_host_label("API Host:");
    Gtk::Entry api_host_entry;
    api_host_entry.set_text("https://api.openai.com/v1");
    api_host_box.pack_start(api_host_label, false, false);
    api_host_box.pack_start(api_host_entry, true, true);
    content_area->pack_start(api_host_box, false, false);
    
    // Model name
    Gtk::Box model_box(Gtk::ORIENTATION_HORIZONTAL, 5);
    Gtk::Label model_label("Model:");
    Gtk::Entry model_entry;
    model_entry.set_text("gpt-4o");
    model_box.pack_start(model_label, false, false);
    model_box.pack_start(model_entry, true, true);
    content_area->pack_start(model_box, false, false);
    
    // Update field sensitivity based on backend type
    backend_combo.signal_changed().connect([&]() {
        if (backend_combo.get_active_id() == "openai") {
            api_host_entry.set_text("https://api.openai.com/v1");
            model_entry.set_text("gpt-4o");
            api_key_entry.set_sensitive(true);
        } else if (backend_combo.get_active_id() == "ollama") {
            api_host_entry.set_text("http://localhost:11434");
            model_entry.set_text("llama3");
            api_key_entry.set_sensitive(false); // No API key needed for local Ollama
        }
    });
    
    // Load settings from configuration file if it exists
    Glib::KeyFile keyfile;
    const std::string fname = "agent_settings.ini";
    if (Glib::file_test(fname, Glib::FILE_TEST_EXISTS)) {
        try {
            keyfile.load_from_file(fname);
            
            if (keyfile.has_group("AI")) {
                if (keyfile.has_key("AI", "backend_type")) {
                    backend_combo.set_active_id(keyfile.get_string("AI", "backend_type"));
                }
                if (keyfile.has_key("AI", "api_key")) {
                    api_key_entry.set_text(keyfile.get_string("AI", "api_key"));
                }
                if (keyfile.has_key("AI", "api_host")) {
                    api_host_entry.set_text(keyfile.get_string("AI", "api_host"));
                }
                if (keyfile.has_key("AI", "model")) {
                    model_entry.set_text(keyfile.get_string("AI", "model"));
                }
            }
        } catch (const Glib::Error& e) {
            add_debug_text("⚠️ Failed to load AI agent settings: " + std::string(e.what()) + "\n");
        }
    }
    
    // Show dialog and handle response
    content_area->show_all();
    int result = dialog.run();
    
    if (result == Gtk::RESPONSE_OK) {
        // Save settings to configuration file
        try {
            keyfile.set_string("AI", "backend_type", backend_combo.get_active_id());
            keyfile.set_string("AI", "api_key", api_key_entry.get_text());
            keyfile.set_string("AI", "api_host", api_host_entry.get_text());
            keyfile.set_string("AI", "model", model_entry.get_text());
            
            // Also save MQTT settings
            keyfile.set_string("MQTT", "host", mqtt_host_entry_.get_text());
            keyfile.set_integer("MQTT", "port", std::stoi(mqtt_port_entry_.get_text()));
            keyfile.set_string("MQTT", "topic", mqtt_topic_entry_.get_text());
            keyfile.set_string("MQTT", "command_topic", mqtt_command_topic_entry_.get_text());
            
            keyfile.save_to_file(fname);
            add_debug_text("✅ Saved AI agent settings to " + fname + "\n");
            
            // If agent is running, notify it to reload settings
            if (!start_agent_button_.get_sensitive()) {
                json settings_message;
                settings_message["type"] = "reload_settings";
                mqtt_client_->publish("sauron/ai/command", settings_message.dump());
                add_debug_text("📨 Notified agent to reload settings\n");
            }
        } catch (const Glib::Error& e) {
            add_debug_text("❌ Failed to save AI agent settings: " + std::string(e.what()) + "\n");
        }
    }
}

// Update the on_panel_capture method to send captures to the ChatPanel
void SauronWindow::on_panel_capture(const std::string& filepath, const std::string& window_title, 
                                   const std::string& trigger_type) {
    // Save filepath for later use
    last_capture_path_ = filepath;
    
    // Enable the send button
    send_button_.set_sensitive(true);
    
    // Publish to MQTT if connected
    if (mqtt_connected_) {
        mqtt_client_->publish_image(mqtt_topic_entry_.get_text(), 
                                   filepath, window_title, trigger_type);
    }
    
    // Add to the chat panel if agent is running
    if (!start_agent_button_.get_sensitive()) {
        chat_panel_.add_capture_message(filepath);
    }
    
    // Refresh thumbnails and update status
    refresh_captures();
    status_bar_.push("Captured: " + filepath + " | " + window_title);
}

// Handle the send button click
void SauronWindow::on_send_clicked() {
    if (last_capture_path_.empty()) {
        Gtk::MessageDialog dialog(*this, "No Capture Available", false, Gtk::MESSAGE_WARNING);
        dialog.set_secondary_text("Take a screenshot or window capture first.");
        dialog.run();
        return;
    }
    
    // Publish to MQTT if connected
    if (mqtt_connected_) {
        mqtt_client_->publish_image(mqtt_topic_entry_.get_text(), 
                                   last_capture_path_, "Manual Send", "button");
        status_bar_.push("Sent: " + last_capture_path_);
    } else {
        Gtk::MessageDialog dialog(*this, "MQTT Not Connected", false, Gtk::MESSAGE_WARNING);
        dialog.set_secondary_text("Please connect to MQTT first to send captures.");
        dialog.run();
    }
    
    // If agent is running, also send to chat panel
    if (!start_agent_button_.get_sensitive()) {
        chat_panel_.add_capture_message(last_capture_path_);
    }
}

// Open the captures folder
void SauronWindow::on_open_folder_clicked() {
    try {
        std::string captures_path = "captures";
        if (!std::filesystem::exists(captures_path)) {
            captures_path = "./captures";
        }
        
        std::string command = "xdg-open " + captures_path;
        Glib::spawn_command_line_async(command);
    } catch (const Glib::Error& e) {
        Gtk::MessageDialog dialog(*this, "Failed to Open Folder", false, Gtk::MESSAGE_ERROR);
        dialog.set_secondary_text("Error: " + std::string(e.what()));
        dialog.run();
    }
}
