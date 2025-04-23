#ifndef OPENAI_BACKEND_H
#define OPENAI_BACKEND_H

#include "AIBackend.h"
#include <string>
#include <memory>
#include <atomic>

/**
 * Implementation of AIBackend for OpenAI API
 */
class OpenAIBackend : public AIBackend {
public:
    OpenAIBackend();
    ~OpenAIBackend() override;
    
    bool initialize(const std::string& api_key, 
                   const std::string& api_host,
                   const std::string& model_name) override;
    
    bool send_message(const std::vector<Message>& messages,
                     const std::string& image_path,
                     ResponseCallback callback) override;
    
    bool is_ready() const override;
    
private:
    std::string api_key_;
    std::string api_host_;
    std::string model_name_;
    std::atomic<bool> initialized_{false};
    
    // Helper methods for API communication
    std::string prepare_request_payload(const std::vector<Message>& messages, const std::string& image_path);
    bool encode_image_base64(const std::string& image_path, std::string& base64_output);
};

#endif // OPENAI_BACKEND_H
