#include <gtkmm/application.h>
#include "../include/SauronWindow.h"

int main(int argc, char *argv[])
{
    auto app = Gtk::Application::create(argc, argv, "org.sauron.eye");
    
    SauronWindow window;
    
    return app->run(window);
}

