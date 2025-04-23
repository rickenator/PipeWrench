#include "../include/AIBackend.h"
#include "../include/OpenAIBackend.h"
#include "../include/OllamaBackend.h"
#include <iostream>

std::shared_ptr<AIBackend> AIBackend::create(const std::string& backend_type) {
    if (backend_type == "openai") {
        return std::make_shared<OpenAIBackend>();
    } else if (backend_type == "ollama") {
        return std::make_shared<OllamaBackend>();
    } else {
        std::cerr << "❌ Unknown AI backend type: " << backend_type << std::endl;
        std::cerr << "   Supported types: openai, ollama" << std::endl;
        return nullptr;
    }
}
