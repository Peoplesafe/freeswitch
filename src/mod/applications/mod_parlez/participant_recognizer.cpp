#include "participant_recognizer.h"
#include "parlez_globals.h"
#include <cstring>

ParticipantRecognizer::ParticipantRecognizer(const std::string& unique_id,
                                           const std::string& source_language,
                                           const std::string& target_language,
                                           const std::string& voice_name,
                                           uint32_t participant_id,
                                           TranslatedAudioCallback audio_callback)
    : unique_id_(unique_id)
    , source_language_(source_language)
    , target_language_(target_language)
    , voice_name_(voice_name)
    , participant_id_(participant_id)
    , is_active_(false)
    , is_recognizing_(false)
    , audio_callback_(audio_callback) {
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Created ParticipantRecognizer %s for participant %u (%s->%s, voice: %s)\n",
                     unique_id_.c_str(), participant_id_, source_language_.c_str(), 
                     target_language_.c_str(), voice_name_.c_str());
}

ParticipantRecognizer::~ParticipantRecognizer() {
    cleanup();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Destroyed ParticipantRecognizer %s\n", unique_id_.c_str());
}

switch_status_t ParticipantRecognizer::initialize(const std::string& subscription_key, const std::string& region) {
    try {
        // Create translation config for this participant
        translation_config_ = SpeechTranslationConfig::FromSubscription(subscription_key, region);
        if (!translation_config_) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                             "mod_parlez: Failed to create SpeechTranslationConfig for %s\n", unique_id_.c_str());
            return SWITCH_STATUS_FALSE;
        }
        
        // Configure Azure Speech SDK logging
        translation_config_->SetProperty(Microsoft::CognitiveServices::Speech::PropertyId::Speech_LogFilename, 
                "/usr/local/freeswitch/var/log/freeswitch/speech_sdk.log");
        
        // Configure audio format for Azure (16kHz, 16-bit, mono PCM)
        auto audio_format = AudioStreamFormat::GetWaveFormatPCM(
            ParlezGlobals::AZURE_SAMPLE_RATE, 16, 1);
        
        // Create push audio input stream for this participant
        audio_input_stream_ = AudioInputStream::CreatePushStream(audio_format);
        if (!audio_input_stream_) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                             "mod_parlez: Failed to create audio input stream for %s\n", unique_id_.c_str());
            return SWITCH_STATUS_FALSE;
        }
        
        audio_config_ = AudioConfig::FromStreamInput(audio_input_stream_);
        if (!audio_config_) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                             "mod_parlez: Failed to create audio config for %s\n", unique_id_.c_str());
            return SWITCH_STATUS_FALSE;
        }
        
        // Setup the recognizer with language configuration
        switch_status_t status = setupRecognizer();
        if (status != SWITCH_STATUS_SUCCESS) {
            return status;
        }
        
        is_active_ = true;
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: ParticipantRecognizer %s initialized successfully\n", unique_id_.c_str());
        
        return SWITCH_STATUS_SUCCESS;
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception initializing ParticipantRecognizer %s: %s\n", 
                         unique_id_.c_str(), e.what());
        return SWITCH_STATUS_FALSE;
    }
}

void ParticipantRecognizer::cleanup() {
    is_active_ = false;
    
    // Stop recognition if running
    stopRecognition();
    
    // Clean up Azure SDK resources
    recognizer_.reset();
    audio_config_.reset();
    audio_input_stream_.reset();
    translation_config_.reset();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: ParticipantRecognizer %s cleanup complete\n", unique_id_.c_str());
}

void ParticipantRecognizer::processAudio(const int16_t* pcm_data, size_t sample_count) {
    if (!is_active_.load() || !audio_input_stream_ || !pcm_data || sample_count == 0) {
        return;
    }
    
    try {
        // Convert to bytes
        size_t byte_count = sample_count * sizeof(int16_t);
        uint8_t* byte_data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(pcm_data));
        
        // Push audio to Azure
        audio_input_stream_->Write(byte_data, static_cast<uint32_t>(byte_count));
        
        // Start recognition if not already running
        if (!is_recognizing_.load()) {
            startRecognition();
        }
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception processing audio for %s: %s\n", 
                         unique_id_.c_str(), e.what());
    }
}

void ParticipantRecognizer::signalEndOfSpeech() {
    if (!is_active_.load() || !audio_input_stream_) {
        return;
    }
    
    try {
        // Send silence buffer to trigger end of speech detection
        std::vector<int16_t> silence(1600, 0); // 100ms of silence at 16kHz
        uint8_t* silence_bytes = reinterpret_cast<uint8_t*>(silence.data());
        audio_input_stream_->Write(silence_bytes, silence.size() * sizeof(int16_t));
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Signaled end of speech for %s\n", unique_id_.c_str());
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: Exception signaling end of speech for %s: %s\n", 
                         unique_id_.c_str(), e.what());
    }
}

switch_status_t ParticipantRecognizer::setupRecognizer() {
    try {
        // Configure source and target languages
        translation_config_->SetSpeechRecognitionLanguage(source_language_);
        translation_config_->AddTargetLanguage(target_language_);
        translation_config_->SetVoiceName(voice_name_);
        
        // Enable speech synthesis
        translation_config_->SetProperty(PropertyId::SpeechServiceConnection_SynthEnableCompressedAudioTransmission, "false");
        translation_config_->SetProperty(PropertyId::SpeechServiceConnection_SynthOutputFormat, "riff-16khz-16bit-mono-pcm");
        
        // Enable detailed results
        translation_config_->SetProperty(PropertyId::SpeechServiceResponse_RequestDetailedResultTrueFalse, "true");
        
        // Configure recognition settings for better telephony
        translation_config_->SetProperty("SpeechServiceConnection_RecognitionMode", "Interactive");
        translation_config_->SetProperty("SpeechServiceConnection_LogLevel", "Debug");
        translation_config_->SetProperty("SpeechServiceConnection_EndSilenceTimeoutMs", "300");
        translation_config_->SetProperty("SpeechServiceConnection_InitialSilenceTimeoutMs", "500");
        translation_config_->SetProperty("SpeechServiceConnection_SegmentationSilenceTimeoutMs", "300");
        
        // Make recognition more responsive
        translation_config_->SetProperty("SpeechServiceConnection_SingleShotMode", "false");
        translation_config_->SetProperty("SpeechServiceConnection_ContinuousRecognitionMode", "true");
        translation_config_->SetProperty("SpeechServiceConnection_EnableSilenceDetection", "true");
        translation_config_->SetProperty("SpeechServiceConnection_SilenceDetectionMode", "Aggressive");
        
        // Audio processing settings
        translation_config_->SetProperty("SpeechServiceConnection_AudioProcessing", "true");
        translation_config_->SetProperty("SpeechServiceConnection_EnableAudioLogging", "true");
        translation_config_->SetProperty("SpeechServiceConnection_AutoDetectSilence", "true");
        
        // Force synthesis to be enabled
        translation_config_->SetProperty("SpeechServiceConnection_EnableSynthesis", "true");
        translation_config_->SetProperty("SpeechServiceConnection_TranslationFeatures", "synthesis");
        translation_config_->SetProperty("SpeechServiceConnection_TranslationVoice", voice_name_);
        translation_config_->SetProperty("SpeechServiceConnection_SynthVoice", voice_name_);
        translation_config_->SetProperty("SpeechServiceConnection_AutoSynthesis", "true");
        translation_config_->SetProperty("SpeechServiceConnection_RequestSynthesis", "true");
        translation_config_->SetProperty("TranslationVoice", voice_name_);
        
        // Additional properties for better recognition
        translation_config_->SetProperty("SpeechServiceConnection_WordLevelTimestamps", "true");
        translation_config_->SetProperty("SpeechServiceConnection_PhraseOutputFormat", "Simple");
        translation_config_->SetProperty("SpeechServiceConnection_StablePartialResultThreshold", "3");
        
        // Create recognizer
        recognizer_ = TranslationRecognizer::FromConfig(translation_config_, audio_config_);
        if (!recognizer_) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                             "mod_parlez: Failed to create translation recognizer for %s\n", unique_id_.c_str());
            return SWITCH_STATUS_FALSE;
        }
        
        // Set up event handlers with participant context
        recognizer_->Recognizing.Connect([this](const TranslationRecognitionEventArgs& e) {
            this->onRecognizing(e);
        });
        
        recognizer_->Recognized.Connect([this](const TranslationRecognitionEventArgs& e) {
            this->onRecognized(e);
        });
        
        recognizer_->Synthesizing.Connect([this](const TranslationSynthesisEventArgs& e) {
            this->onSynthesizing(e);
        });
        
        recognizer_->Canceled.Connect([this](const TranslationRecognitionCanceledEventArgs& e) {
            this->onCanceled(e);
        });
        
        // Add session event handlers for debugging
        recognizer_->SessionStarted.Connect([this](const SessionEventArgs& e) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Azure session started for %s - SessionId: %s\n", 
                             unique_id_.c_str(), e.SessionId.c_str());
        });
        
        recognizer_->SessionStopped.Connect([this](const SessionEventArgs& e) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Azure session stopped for %s - SessionId: %s\n", 
                             unique_id_.c_str(), e.SessionId.c_str());
        });
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Translation recognizer setup complete for %s - %s to %s with voice %s\n",
                         unique_id_.c_str(), source_language_.c_str(), target_language_.c_str(), voice_name_.c_str());
        
        return SWITCH_STATUS_SUCCESS;
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception setting up recognizer for %s: %s\n", 
                         unique_id_.c_str(), e.what());
        return SWITCH_STATUS_FALSE;
    }
}

switch_status_t ParticipantRecognizer::startRecognition() {
    if (!recognizer_ || is_recognizing_.load()) {
        return SWITCH_STATUS_SUCCESS;
    }
    
    try {
        recognizer_->StartContinuousRecognitionAsync().wait();
        is_recognizing_ = true;
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Started continuous recognition for %s\n", unique_id_.c_str());
        
        return SWITCH_STATUS_SUCCESS;
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Failed to start recognition for %s: %s\n", 
                         unique_id_.c_str(), e.what());
        return SWITCH_STATUS_FALSE;
    }
}

void ParticipantRecognizer::stopRecognition() {
    if (!recognizer_ || !is_recognizing_.load()) {
        return;
    }
    
    try {
        recognizer_->StopContinuousRecognitionAsync().wait();
        is_recognizing_ = false;
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Stopped continuous recognition for %s\n", unique_id_.c_str());
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: Exception stopping recognition for %s: %s\n", 
                         unique_id_.c_str(), e.what());
    }
}

void ParticipantRecognizer::onRecognizing(const TranslationRecognitionEventArgs& e) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: onRecognizing event for %s (Reason: %d)\n", 
                     unique_id_.c_str(), static_cast<int>(e.Result->Reason));
    if (e.Result->Reason == ResultReason::TranslatingSpeech) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Recognizing from %s: '%s'\n", unique_id_.c_str(), e.Result->Text.c_str());
        // Log partial translations if available
        for (const auto& translation : e.Result->Translations) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Partial translation from %s [%s]: '%s'\n",
                             unique_id_.c_str(), translation.first.c_str(), translation.second.c_str());
        }
    }
}

void ParticipantRecognizer::onRecognized(const TranslationRecognitionEventArgs& e) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: onRecognized event for %s (Reason: %d)\n", 
                     unique_id_.c_str(), static_cast<int>(e.Result->Reason));
    if (e.Result->Reason == ResultReason::TranslatedSpeech) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Recognized from %s: %s\n", unique_id_.c_str(), e.Result->Text.c_str());
        // Log translations
        for (const auto& translation : e.Result->Translations) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Translation from %s [%s]: %s\n",
                             unique_id_.c_str(), translation.first.c_str(), translation.second.c_str());
        }
        
        // Check if synthesis should be triggered
        if (!e.Result->Translations.empty()) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Translation complete for %s - synthesis should trigger now\n", unique_id_.c_str());
        }
    } else if (e.Result->Reason == ResultReason::NoMatch) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: No speech recognized from %s\n", unique_id_.c_str());
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: Unexpected recognition result reason from %s: %d\n", 
                         unique_id_.c_str(), static_cast<int>(e.Result->Reason));
    }
}

void ParticipantRecognizer::onSynthesizing(const TranslationSynthesisEventArgs& e) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "mod_parlez: onSynthesizing event for %s (Reason: %d)\n", 
        unique_id_.c_str(), static_cast<int>(e.Result->Reason));
    
    // Log detailed synthesis result information
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "mod_parlez: Synthesis result for %s - Audio size: %zu bytes\n", 
        unique_id_.c_str(), e.Result->Audio.size());
    
    if (!e.Result->Audio.empty()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "mod_parlez: SUCCESS! Received synthesized audio for %s: %zu bytes\n", 
            unique_id_.c_str(), e.Result->Audio.size());
        
        // Call the callback to handle the translated audio
        if (audio_callback_) {
            audio_callback_(e.Result->Audio, participant_id_);
        }
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "mod_parlez: Synthesis event fired for %s but no audio data received!\n", unique_id_.c_str());
    }
}

void ParticipantRecognizer::onCanceled(const TranslationRecognitionCanceledEventArgs& e) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                     "mod_parlez: onCanceled event for %s - Reason: %d, Error: %s, ErrorCode: %d\n",
                     unique_id_.c_str(), static_cast<int>(e.Reason), e.ErrorDetails.c_str(), static_cast<int>(e.ErrorCode));
}
