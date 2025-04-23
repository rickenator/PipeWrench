#include "../include/SauronAgent.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // Initialize GTK
    Gtk::Main kit(argc, argv);
    
    // Create and initialize SauronAgent
    SauronAgent agent;
    if (!agent.initialize(argc, argv)) {
        std::cerr << "Failed to initialize SauronAgent" << std::endl;
        return 1;
    }
    
    // Run the agent
    agent.run();
    
    return 0;
}
