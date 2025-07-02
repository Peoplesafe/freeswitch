#ifndef TRANSLATION_SESSION_H
#define TRANSLATION_SESSION_H

#include <switch.h>
#include <speechapi_cxx.h>
#include "audio_resampler.h"
#include "audio_injection_manager.h"
#include "participant_recognizer.h"
#include <memory>
#include <string>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <thread>
#include <map>

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;
using namespace Microsoft::CognitiveServices::Speech::Translation;

// Forward declaration
class ParlezGlobals;

struct ParticipantInfo {
    uint32_t member_id;
    std::string caller_id;
    std::string language_code;  // e.g., "en-US", "fr-FR"
    std::string voice_name;     // e.g., "en-US-JennyNeural", "fr-FR-DeniseNeural"
    bool is_talking = false;
    switch_media_bug_t* media_bug = nullptr;
    std::string recognizer_id;  // Unique identifier for this participant's recognizer
};

struct AudioChunk {
    std::vector<uint8_t> data;
    uint32_t timestamp;
};

class TranslationSession {
public:
    TranslationSession(const std::string& conference_name);
    ~TranslationSession();
    
    // Lifecycle
    switch_status_t initialize();
    void cleanup();
    
    // Participant management
    switch_status_t addParticipant(uint32_t member_id, const std::string& caller_id, 
                                   const std::string& language_code, const std::string& voice_name);
    void removeParticipant(uint32_t member_id);
    ParticipantInfo* getParticipant(uint32_t member_id);
    
    // Event handlers
    void onStartTalking(uint32_t member_id);
    void onStopTalking(uint32_t member_id);
    
    // Audio processing
    switch_status_t attachMediaBug(switch_core_session_t* session, uint32_t member_id);
    void detachMediaBug(uint32_t member_id);
    
    // Media bug callback - static function
    static switch_bool_t mediaBugCallback(switch_media_bug_t* bug, void* user_data, 
                                         switch_abc_type_t type);
    
    // Instance method called by static callback
    switch_bool_t processAudioFrame(switch_media_bug_t* bug, switch_abc_type_t type);
    
private:
    // Per-participant recognizer management
    switch_status_t createRecognizerForParticipant(uint32_t member_id);
    void removeRecognizerForParticipant(uint32_t member_id);
    std::shared_ptr<ParticipantRecognizer> getRecognizerForParticipant(uint32_t member_id);
    
    // Audio processing (per-participant)
    void processAudioChunkWithRate(const uint8_t* audio_data, size_t length, 
                                  uint32_t sample_rate, uint32_t member_id);
    
    // Callback for translated audio from recognizers
    void onTranslatedAudio(const std::vector<uint8_t>& audio_data, uint32_t source_participant_id);
    
    // Audio injection to specific participants
    void playTranslatedAudio(const std::vector<uint8_t>& audio_data, uint32_t target_member_id);
    
    // Thread management
    void audioProcessingThread();
    
private:
    std::string conference_name_;
    std::mutex participants_mutex_;
    std::map<uint32_t, ParticipantInfo> participants_;
    
    // Per-participant recognizers (keyed by member_id)
    std::mutex recognizers_mutex_;
    std::map<uint32_t, std::shared_ptr<ParticipantRecognizer>> recognizers_;
    
    // Azure credentials (shared for creating participant recognizers)
    std::string azure_subscription_key_;
    std::string azure_region_;
    
    // Audio processing
    std::mutex audio_queue_mutex_;
    std::queue<AudioChunk> audio_queue_;
    std::atomic<bool> processing_active_;
    std::thread processing_thread_;
    
    // Current state
    uint32_t current_speaker_id_ = 0;
    
    // Audio buffers and resampling
    std::vector<int16_t> pcm_buffer_;
    AudioResampler audio_resampler_;
    
    // Audio injection manager
    std::unique_ptr<AudioInjectionManager> injection_manager_;
    
    // Configuration
    static const size_t MAX_AUDIO_BUFFER_SIZE = 32000; // ~2 seconds at 16kHz
    static const size_t AZURE_CHUNK_SIZE = 3200;       // 100ms at 16kHz, 16-bit
};

#endif // TRANSLATION_SESSION_H