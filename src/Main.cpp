#include "MyWindow.h"
#include <gtkmm/application.h>
#include <iostream>
#include <filesystem>

bool ensure_captures_directory() {
    const std::string capturesDir = "captures";
    if (!std::filesystem::exists(capturesDir)) {
        std::cout << "📁 Creating captures directory..." << std::endl;
        try {
            std::filesystem::create_directory(capturesDir);
            std::cout << "✅ Created captures directory" << std::endl;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "❌ Failed to create captures directory: " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (!ensure_captures_directory())
        return 1;

    std::cout << "🔧 Initializing Gtk::Application..." << std::endl;
    auto app = Gtk::Application::create(argc, argv, "org.example.PipeWrench");

    app->signal_activate().connect([&]() {
        auto window = new MyWindow();
        app->add_window(*window);
        window->show();
    });

    std::cout << "🚀 Running application event loop..." << std::endl;
    return app->run();
}

