#ifndef OLLAMA_BACKEND_H
#define OLLAMA_BACKEND_H

#include "AIBackend.h"
#include <string>
#include <memory>
#include <atomic>

/**
 * Implementation of AIBackend for Ollama API (local AI models)
 */
class OllamaBackend : public AIBackend {
public:
    OllamaBackend();
    ~OllamaBackend() override;
    
    bool initialize(const std::string& api_key, 
                   const std::string& api_host,
                   const std::string& model_name) override;
    
    bool send_message(const std::vector<Message>& messages,
                     const std::string& image_path,
                     ResponseCallback callback) override;
    
    bool is_ready() const override;
    
private:
    std::string api_host_;  // Ollama API host (typically http://localhost:11434)
    std::string model_name_; // Model name (e.g., llama3, mistral, etc.)
    std::atomic<bool> initialized_{false};
    
    // Helper methods for API communication
    std::string prepare_request_payload(const std::vector<Message>& messages, const std::string& image_path);
    bool encode_image_base64(const std::string& image_path, std::string& base64_output);
};

#endif // OLLAMA_BACKEND_H
