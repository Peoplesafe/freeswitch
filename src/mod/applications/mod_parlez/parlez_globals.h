#ifndef PARLEZ_GLOBALS_H
#define PARLEZ_GLOBALS_H

#include <switch.h>
#include <map>
#include <string>
#include <memory>
#include <mutex>

// Forward declarations
class TranslationSession;

// Global state management for mod_parlez
class ParlezGlobals {
public:
    static ParlezGlobals& getInstance();
    
    // Initialize global state
    switch_status_t initialize();
    
    // Cleanup global state
    void cleanup();
    
    // Session management
    void addSession(const std::string& conference_name, std::shared_ptr<TranslationSession> session);
    void removeSession(const std::string& conference_name);
    std::shared_ptr<TranslationSession> getSession(const std::string& conference_name);
    
    // Configuration
    void setAzureSubscriptionKey(const std::string& key);
    void setAzureRegion(const std::string& region);
    std::string getAzureSubscriptionKey() const;
    std::string getAzureRegion() const;
    
    // Audio format constants
    static const int FREESWITCH_SAMPLE_RATE = 8000;
    static const int AZURE_SAMPLE_RATE = 16000;  // Back to 16kHz for better recognition
    static const int SAMPLES_PER_FRAME = 160;  // 20ms at 8kHz
    static const int BYTES_PER_SAMPLE = 2;    // 16-bit PCM
    
private:
    ParlezGlobals() = default;
    ~ParlezGlobals() = default;
    ParlezGlobals(const ParlezGlobals&) = delete;
    ParlezGlobals& operator=(const ParlezGlobals&) = delete;
    
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<TranslationSession>> sessions_;
    std::string azure_subscription_key_;
    std::string azure_region_;
    bool initialized_ = false;
};

#endif // PARLEZ_GLOBALS_H