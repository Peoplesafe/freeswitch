#include "translation_session.h"
#include "parlez_globals.h"
#include <cstring>
#include <algorithm>

TranslationSession::TranslationSession(const std::string& conference_name)
    : conference_name_(conference_name)
    , processing_active_(false) {
    
    // Initialize audio injection manager
    injection_manager_ = std::make_unique<AudioInjectionManager>();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Created translation session for conference: %s\n",
                     conference_name_.c_str());
}

TranslationSession::~TranslationSession() {
    cleanup();
}

switch_status_t TranslationSession::initialize() {
    try {
        // Get Azure configuration
        auto& globals = ParlezGlobals::getInstance();
        azure_subscription_key_ = globals.getAzureSubscriptionKey();
        azure_region_ = globals.getAzureRegion();
        
        if (azure_subscription_key_.empty()) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                             "mod_parlez: Azure subscription key not configured\n");
            return SWITCH_STATUS_FALSE;
        }
        
        // Initialize audio buffers
        pcm_buffer_.reserve(MAX_AUDIO_BUFFER_SIZE);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Translation session initialized for %s\n",
                         conference_name_.c_str());
        
        return SWITCH_STATUS_SUCCESS;
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception during initialization: %s\n", e.what());
        return SWITCH_STATUS_FALSE;
    }
}

void TranslationSession::cleanup() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: Cleaning up translation session for %s\n",
                     conference_name_.c_str());
    
    // Stop processing
    processing_active_ = false;
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    // Clean up participants and media bugs
    std::lock_guard<std::mutex> participants_lock(participants_mutex_);
    for (auto& pair : participants_) {
        detachMediaBug(pair.first);
    }
    participants_.clear();
    
    // Clean up all recognizers
    std::lock_guard<std::mutex> recognizers_lock(recognizers_mutex_);
    for (auto& pair : recognizers_) {
        if (pair.second) {
            pair.second->cleanup();
        }
    }
    recognizers_.clear();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: Translation session cleanup complete\n");
}

switch_status_t TranslationSession::addParticipant(uint32_t member_id, 
                                                  const std::string& caller_id,
                                                  const std::string& language_code,
                                                  const std::string& voice_name) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    try
    {

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                        "mod_parlez: Adding participant to session %u (%s) with language %s and voice %s\n",
                        member_id, caller_id.c_str(), language_code.c_str(), voice_name.c_str());
        
        ParticipantInfo participant;
        participant.member_id = member_id;
        participant.caller_id = caller_id;
        participant.language_code = language_code;
        participant.voice_name = voice_name;
        participant.is_talking = false;
        participant.media_bug = nullptr;
        
        // Create unique recognizer ID for this participant
        participant.recognizer_id = "participant_" + conference_name_ + "_" + std::to_string(member_id);
        participants_[member_id] = participant;

        // Create recognizer for this participant if we have a target language
        createRecognizerForParticipant(member_id);

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                        "mod_parlez: Added participant %u (%s) with language %s, recognizer_id: %s\n",
                        member_id, caller_id.c_str(), language_code.c_str(), participant.recognizer_id.c_str());
        
        return SWITCH_STATUS_SUCCESS;
    }
    catch(const std::exception& e)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                        "mod_parlez: Exception adding participant %u: %s\n", member_id, e.what());
        return SWITCH_STATUS_FALSE;
    }
}

void TranslationSession::removeParticipant(uint32_t member_id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    
    auto participant = participants_.find(member_id);
    if (participant != participants_.end()) {
        // Remove the participant's recognizer
        removeRecognizerForParticipant(member_id);
        
        detachMediaBug(member_id);
        participants_.erase(participant);
        
        // Clean up injection queue
        injection_manager_->removeParticipant(member_id);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Removed participant %u and their recognizer\n", member_id);
    }
}

ParticipantInfo* TranslationSession::getParticipant(uint32_t member_id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    
    auto participant = participants_.find(member_id);
    return (participant != participants_.end()) ? &participant->second : nullptr;
}

void TranslationSession::onStartTalking(uint32_t member_id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    
    auto participant = participants_.find(member_id);
    if (participant != participants_.end()) {
        participant->second.is_talking = true;
        current_speaker_id_ = member_id;
        
        // Get the speaking participant's recognizer for logging
        auto recognizer = getRecognizerForParticipant(member_id);
        
        if (recognizer) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Using recognizer %s for participant %u\n",
                             recognizer->getUniqueId().c_str(), member_id);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                             "mod_parlez: No recognizer found for participant %u\n", member_id);
        }
    
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Participant %u started talking\n", member_id);
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Participant %u not found in participants list\n", member_id);
    }
}

void TranslationSession::onStopTalking(uint32_t member_id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    
    auto participant = participants_.find(member_id);
    if (participant != participants_.end()) {
        participant->second.is_talking = false;
        
        if (current_speaker_id_ == member_id) {
            current_speaker_id_ = 0;
            
            // Signal end of speech to the participant's recognizer
            auto recognizer = getRecognizerForParticipant(member_id);
            if (recognizer && recognizer->isActive()) {
                recognizer->signalEndOfSpeech();
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                 "mod_parlez: Signaled end of speech to recognizer %s\n",
                                 recognizer->getUniqueId().c_str());
            }
            
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                             "mod_parlez: Participant %u stopped talking (signaled end to recognizer)\n", 
                             member_id);
        }
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                         "mod_parlez: Participant %u stopped talking\n", member_id);
    }
}

switch_status_t TranslationSession::createRecognizerForParticipant(uint32_t member_id) {
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: Creating recognizer for participant %u\n", member_id);

    auto participant_it = participants_.find(member_id);
    if (participant_it == participants_.end()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Participant %u not found for recognizer creation\n", member_id);
        return SWITCH_STATUS_FALSE;
    }
    
    auto& participant = participant_it->second;
    
    // Find a target participant with a different language
    std::string target_language;
    std::string target_voice;
    for (const auto& other_participant : participants_) {
        if (other_participant.first != member_id && 
            other_participant.second.language_code != participant.language_code) {
            target_language = other_participant.second.language_code;
            target_voice = other_participant.second.voice_name;
            break;
        }
    }
    
    // default to English if no target language found
    if (target_language.empty()) {
        if (strcasecmp(participant.language_code.c_str(), "en-GB")){
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: No target language found for participant %u with language_code %s, defaulting to English\n", 
                         member_id, participant.language_code.c_str());
            target_language = "en-GB"; // Default to English if no target language found
            target_voice = "en-GB-LibbyNeural"; // Default voice
        }else{
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: No target language found for participant %u with language_code %s, skipping recognizer creation\n", 
                         member_id, participant.language_code.c_str());
            return SWITCH_STATUS_SUCCESS;
        }            
    }
    
    // Create callback for translated audio
    auto audio_callback = [this](const std::vector<uint8_t>& audio_data, uint32_t source_participant_id) {
        this->onTranslatedAudio(audio_data, source_participant_id);
    };
    
    // Create the recognizer
    auto recognizer = std::make_shared<ParticipantRecognizer>(
        participant.recognizer_id,
        participant.language_code,
        target_language,
        target_voice,
        member_id,
        audio_callback
    );
    
    // Initialize the recognizer
    switch_status_t status = recognizer->initialize(azure_subscription_key_, azure_region_);
    if (status != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Failed to initialize recognizer for participant %u\n", member_id);
        return status;
    }
    
    // Store the recognizer
    std::lock_guard<std::mutex> recognizers_lock(recognizers_mutex_);
    recognizers_[member_id] = recognizer;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: Created recognizer %s for participant %u (%s->%s)\n",
                     participant.recognizer_id.c_str(), member_id, 
                     participant.language_code.c_str(), target_language.c_str());
    
    return SWITCH_STATUS_SUCCESS;
}

void TranslationSession::removeRecognizerForParticipant(uint32_t member_id) {
    std::lock_guard<std::mutex> recognizers_lock(recognizers_mutex_);
    
    auto recognizer_it = recognizers_.find(member_id);
    if (recognizer_it != recognizers_.end()) {
        if (recognizer_it->second) {
            recognizer_it->second->cleanup();
        }
        recognizers_.erase(recognizer_it);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Removed recognizer for participant %u\n", member_id);
    }
}

std::shared_ptr<ParticipantRecognizer> TranslationSession::getRecognizerForParticipant(uint32_t member_id) {
    std::lock_guard<std::mutex> recognizers_lock(recognizers_mutex_);
    
    auto recognizer_it = recognizers_.find(member_id);
    if (recognizer_it != recognizers_.end()) {
        return recognizer_it->second;
    }
    
    return nullptr;
}

switch_status_t TranslationSession::attachMediaBug(switch_core_session_t* session, uint32_t member_id) {
    std::lock_guard<std::mutex> lock(participants_mutex_);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: Attaching media bug for participant %u\n", member_id);

    auto participant = participants_.find(member_id);
    if (participant == participants_.end()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Participant %u not found for media bug attachment\n",
                         member_id);
        return SWITCH_STATUS_FALSE;
    }
    
    if (participant->second.media_bug) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: Media bug already attached for participant %u\n",
                         member_id);
        return SWITCH_STATUS_SUCCESS;
    }
    
    switch_media_bug_flag_t flags = SMBF_READ_REPLACE | SMBF_WRITE_REPLACE | SMBF_NO_PAUSE;
    switch_status_t status = switch_core_media_bug_add(
        session, "parlez", NULL,
        mediaBugCallback, this, 0, flags,
        &participant->second.media_bug);
    
    if (status != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Failed to attach media bug for participant %u\n",
                         member_id);
        return status;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                     "mod_parlez: Media bug attached for participant %u\n", member_id);
    
    return SWITCH_STATUS_SUCCESS;
}

void TranslationSession::detachMediaBug(uint32_t member_id) {
    auto participant = participants_.find(member_id);
    if (participant != participants_.end() && participant->second.media_bug) {
        switch_core_media_bug_remove(switch_core_media_bug_get_session(participant->second.media_bug),
                                    &participant->second.media_bug);
        participant->second.media_bug = nullptr;
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                         "mod_parlez: Media bug detached for participant %u\n", member_id);
    }
}

switch_bool_t TranslationSession::mediaBugCallback(switch_media_bug_t* bug, void* user_data,
                                                  switch_abc_type_t type) {
    TranslationSession* session = static_cast<TranslationSession*>(user_data);
    return session->processAudioFrame(bug, type);
}

switch_bool_t TranslationSession::processAudioFrame(switch_media_bug_t* bug, switch_abc_type_t type) {
    switch (type) {
        case SWITCH_ABC_TYPE_INIT:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                             "mod_parlez: Media bug initialized\n");
            break;
            
        case SWITCH_ABC_TYPE_CLOSE:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                             "mod_parlez: Media bug closing\n");
            break;
            
        case SWITCH_ABC_TYPE_READ_REPLACE:
        {
            switch_frame_t* frame = switch_core_media_bug_get_read_replace_frame(bug);
            if (frame && frame->data && frame->datalen > 0) {
                
                // Log frame details for debugging (once)
                static bool frame_details_logged = false;
                if (!frame_details_logged) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                     "mod_parlez: Frame details - samples: %u, datalen: %u, rate: %u, channels: %u\n",
                                     frame->samples, frame->datalen, frame->rate, frame->channels);
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                     "mod_parlez: Expected format: S16LE PCM at %u Hz\n", frame->rate);
                    frame_details_logged = true;
                }
                
                // Find which participant this bug belongs to
                uint32_t member_id = 0;
                {
                    std::lock_guard<std::mutex> lock(participants_mutex_);
                    for (const auto& pair : participants_) {
                        if (pair.second.media_bug == bug) {
                            member_id = pair.first;
                            break;
                        }
                    }
                }
                
                if (member_id > 0 && member_id == current_speaker_id_) {
                    // Process audio from the current speaker
                    // Pass the frame rate so we can handle resampling correctly
                    processAudioChunkWithRate(static_cast<uint8_t*>(frame->data), 
                                            frame->datalen, frame->rate, member_id);
                }
            }
            break;
        }
        
        case SWITCH_ABC_TYPE_WRITE_REPLACE:
        {
            switch_frame_t* frame = switch_core_media_bug_get_write_replace_frame(bug);
            if (frame && frame->data && frame->datalen > 0) {
                
                // Find which participant this bug belongs to
                uint32_t member_id = 0;
                {
                    std::lock_guard<std::mutex> lock(participants_mutex_);
                    for (const auto& pair : participants_) {
                        if (pair.second.media_bug == bug) {
                            member_id = pair.first;
                            break;
                        }
                    }
                }
                
                if (member_id > 0) {
                    // Check if we have audio to inject for this participant
                    injection_manager_->injectQueuedAudio(member_id, frame);
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    return SWITCH_TRUE;
}

void TranslationSession::processAudioChunkWithRate(const uint8_t* audio_data, size_t length, 
                                                  uint32_t sample_rate, uint32_t member_id) {
    if (!audio_data || length == 0) {
        return;
    }
    
    // Only process audio for participants who are currently talking
    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        auto participant_it = participants_.find(member_id);
        if (participant_it == participants_.end() || !participant_it->second.is_talking) {
            return;
        }
    }
    
    // Get the recognizer for this participant
    auto recognizer = getRecognizerForParticipant(member_id);
    if (!recognizer || !recognizer->isActive()) {
        return;
    }

    // Log first time we process audio for debugging
    static bool first_audio_logged = false;
    if (!first_audio_logged) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: First audio chunk - length: %zu bytes, rate: %u Hz, member: %u\n",
                         length, sample_rate, member_id);
        first_audio_logged = true;
    }

    // Validate 16-bit PCM format
    if (length % 2 != 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: Odd byte count (%zu) - expected 16-bit PCM\n", length);
        return;
    }
    
    size_t sample_count = length / 2;  // 16-bit = 2 bytes per sample
    const int16_t* input_pcm = reinterpret_cast<const int16_t*>(audio_data);
    
    // Use AudioResampler to convert to 16kHz
    size_t resampled_sample_count;
    const int16_t* resampled_audio = audio_resampler_.resampleTo16kHz(
        input_pcm, sample_count, sample_rate, &resampled_sample_count);
    
    if (!resampled_audio || resampled_sample_count == 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: Failed to resample audio from %u Hz\n", sample_rate);
        return;
    }
    
    // Log resampling activity periodically
    static int audio_chunk_count = 0;
    audio_chunk_count++;
    if (audio_chunk_count % 100 == 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Processed %d audio chunks, latest: %zu->%zu samples @ %u Hz\n",
                         audio_chunk_count, sample_count, resampled_sample_count, sample_rate);
    }
    
    // Send the resampled audio to the participant's recognizer
    recognizer->processAudio(resampled_audio, resampled_sample_count);
}

void TranslationSession::onTranslatedAudio(const std::vector<uint8_t>& audio_data, uint32_t source_participant_id) {
    if (audio_data.empty()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: onTranslatedAudio called with empty audio data\n");
        return;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: onTranslatedAudio called - source_participant_id: %u, audio_data size: %zu\n",
                     source_participant_id, audio_data.size());
    
    // Get list of target participants while holding the lock
    std::vector<uint32_t> target_participants;
    {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        for (const auto& participant : participants_) {
            if (participant.first != source_participant_id) {
                target_participants.push_back(participant.first);
            }
        }
    }
    
    // Send translated audio to all target participants (without holding the lock)
    for (uint32_t target_id : target_participants) {
        playTranslatedAudio(audio_data, target_id);
    }
}

void TranslationSession::playTranslatedAudio(const std::vector<uint8_t>& audio_data, uint32_t target_member_id) {
    if (audio_data.empty()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: playTranslatedAudio called with empty audio data\n");
        return;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                     "mod_parlez: playTranslatedAudio called - target_member_id: %u, audio_data size: %zu\n",
                     target_member_id, audio_data.size());
    
    // If target_member_id is 0, find the other participant (not the current speaker)
    uint32_t actual_target_id = target_member_id;
    if (actual_target_id == 0) {
        std::lock_guard<std::mutex> lock(participants_mutex_);
        for (const auto& pair : participants_) {
            if (pair.first != current_speaker_id_) {
                actual_target_id = pair.first;
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                 "mod_parlez: Auto-selected target participant %u (speaker is %u)\n",
                                 actual_target_id, current_speaker_id_);
                break;
            }
        }
    }
    
    if (actual_target_id == 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "mod_parlez: No target participant found for audio injection\n");
        return;
    }
    
    try {
        // Azure returns audio in WAV format at 16kHz, extract PCM data
        // Skip WAV header (44 bytes) and get the PCM data
        if (audio_data.size() <= 44) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                             "mod_parlez: Audio data too small: %zu bytes\n", audio_data.size());
            return;
        }
        
        const uint8_t* pcm_start = audio_data.data() + 44; // Skip WAV header
        size_t pcm_size = audio_data.size() - 44;
        size_t pcm_sample_count = pcm_size / sizeof(int16_t);
        const int16_t* pcm_data = reinterpret_cast<const int16_t*>(pcm_start);
        
        // Resample from 16kHz to target sample rate for FreeSWITCH injection
        // TODO: Get actual target sample rate from participant's frame rate
        uint32_t target_sample_rate = 8000;  // Most common for telephony
        
        size_t resampled_count;
        const int16_t* resampled_pcm = audio_resampler_.resampleFrom16kHz(
            pcm_data, pcm_sample_count, target_sample_rate, &resampled_count);
        
        if (!resampled_pcm || resampled_count == 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                             "mod_parlez: Failed to resample translated audio to %u Hz\n", target_sample_rate);
            return;
        }
        
        // Convert PCM16 to bytes for injection (FreeSWITCH expects S16LE PCM, not μ-law)
        std::vector<uint8_t> pcm_bytes(resampled_count * sizeof(int16_t));
        memcpy(pcm_bytes.data(), resampled_pcm, pcm_bytes.size());
        
        // Check if target participant exists and has media bug (with minimal lock scope)
        bool target_exists = false;
        {
            std::lock_guard<std::mutex> lock(participants_mutex_);
            auto it = participants_.find(actual_target_id);
            target_exists = (it != participants_.end() && it->second.media_bug);
        }
        
        if (target_exists) {
            // Queue the translated audio for injection (as PCM16 bytes)
            injection_manager_->queueAudioForInjection(actual_target_id, pcm_bytes);
            
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                             "mod_parlez: Queued translated audio for participant %u (%zu bytes S16LE PCM)\n",
                             actual_target_id, pcm_bytes.size());
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                             "mod_parlez: Target participant %u not found or no media bug attached\n",
                             actual_target_id);
        }
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception playing translated audio: %s\n", e.what());
    }
}