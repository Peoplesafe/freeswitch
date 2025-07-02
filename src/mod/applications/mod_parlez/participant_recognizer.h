#ifndef PARTICIPANT_RECOGNIZER_H
#define PARTICIPANT_RECOGNIZER_H

#include <switch.h>
#include <speechapi_cxx.h>
#include "audio_resampler.h"
#include <memory>
#include <string>
#include <atomic>
#include <functional>
#include <vector>

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;
using namespace Microsoft::CognitiveServices::Speech::Translation;

// Forward declaration
class TranslationSession;

// Callback type for when translated audio is ready
using TranslatedAudioCallback = std::function<void(const std::vector<uint8_t>&, uint32_t)>;

class ParticipantRecognizer {
public:
    ParticipantRecognizer(const std::string& unique_id,
                         const std::string& source_language,
                         const std::string& target_language,
                         const std::string& voice_name,
                         uint32_t participant_id,
                         TranslatedAudioCallback audio_callback);
    
    ~ParticipantRecognizer();
    
    // Lifecycle management
    switch_status_t initialize(const std::string& subscription_key, const std::string& region);
    void cleanup();
    
    // Audio processing
    void processAudio(const int16_t* pcm_data, size_t sample_count);
    void signalEndOfSpeech();
    
    // State management
    bool isActive() const { return is_active_.load(); }
    const std::string& getUniqueId() const { return unique_id_; }
    uint32_t getParticipantId() const { return participant_id_; }
    
    // Language configuration
    const std::string& getSourceLanguage() const { return source_language_; }
    const std::string& getTargetLanguage() const { return target_language_; }
    const std::string& getVoiceName() const { return voice_name_; }

private:
    // Azure Speech SDK components
    std::shared_ptr<SpeechTranslationConfig> translation_config_;
    std::shared_ptr<AudioConfig> audio_config_;
    std::shared_ptr<TranslationRecognizer> recognizer_;
    std::shared_ptr<PushAudioInputStream> audio_input_stream_;
    
    // Configuration
    std::string unique_id_;
    std::string source_language_;
    std::string target_language_;
    std::string voice_name_;
    uint32_t participant_id_;
    
    // State
    std::atomic<bool> is_active_;
    std::atomic<bool> is_recognizing_;
    
    // Callback for translated audio
    TranslatedAudioCallback audio_callback_;
    
    // Audio processing
    AudioResampler audio_resampler_;
    
    // Event handlers
    void onRecognizing(const TranslationRecognitionEventArgs& e);
    void onRecognized(const TranslationRecognitionEventArgs& e);
    void onSynthesizing(const TranslationSynthesisEventArgs& e);
    void onCanceled(const TranslationRecognitionCanceledEventArgs& e);
    
    // Helper methods
    switch_status_t setupRecognizer();
    switch_status_t startRecognition();
    void stopRecognition();
};

#endif // PARTICIPANT_RECOGNIZER_H
