#ifndef AI_BACKEND_H
#define AI_BACKEND_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declaration
struct Message;

/**
 * Abstract base class for AI backend implementations
 */
class AIBackend {
public:
    virtual ~AIBackend() = default;
    
    // Type of callback for responses
    using ResponseCallback = std::function<void(const std::string&, bool)>;
    
    /**
     * Initialize the backend with the required parameters
     * @param api_key API key for the service (if needed)
     * @param api_host Host URL for the service
     * @param model_name Model to use for generation
     * @return True if initialization is successful
     */
    virtual bool initialize(const std::string& api_key, 
                           const std::string& api_host,
                           const std::string& model_name) = 0;
    
    /**
     * Send a message to the AI backend
     * @param messages Vector of previous messages for context
     * @param image_path Optional path to an image to include in the message
     * @param callback Function to call when response is received
     * @return True if request was successfully sent
     */
    virtual bool send_message(const std::vector<Message>& messages,
                             const std::string& image_path,
                             ResponseCallback callback) = 0;
    
    /**
     * Check if the backend is initialized and ready
     * @return True if backend is ready
     */
    virtual bool is_ready() const = 0;
    
    /**
     * Create an appropriate backend based on the type string
     * @param backend_type Type of backend to create ("openai", "ollama", etc.)
     * @return Shared pointer to the created backend
     */
    static std::shared_ptr<AIBackend> create(const std::string& backend_type);
};

#endif // AI_BACKEND_H
