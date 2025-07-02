#include "audio_injection_manager.h"
#include <algorithm>
#include <cstring>

AudioInjectionManager::AudioInjectionManager() {
}

AudioInjectionManager::~AudioInjectionManager() {
    std::lock_guard<std::mutex> lock(injection_mutex_);
    injection_queues_.clear();
}

void AudioInjectionManager::queueAudioForInjection(uint32_t member_id, const std::vector<uint8_t>& pcm_data) {
    try {
        std::lock_guard<std::mutex> lock(injection_mutex_);
        
        // Create injection audio struct
        InjectionAudio injection_audio;
        injection_audio.pcm_data = pcm_data;  // Actually PCM data, not μ-law
        injection_audio.position = 0;
        injection_audio.timestamp = switch_micro_time_now() / 1000; // Convert to milliseconds
        
        // Add to the participant's injection queue
        injection_queues_[member_id].push(injection_audio);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                         "mod_parlez: Queued audio injection for participant %u: %zu bytes S16LE PCM (queue size: %zu)\n",
                         member_id, pcm_data.size(), injection_queues_[member_id].size());
                         
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception queueing audio for injection: %s\n", e.what());
    }
}

void AudioInjectionManager::injectQueuedAudio(uint32_t member_id, switch_frame_t* frame) {
    try {
        std::lock_guard<std::mutex> lock(injection_mutex_);
        
        auto& queue = injection_queues_[member_id];
        if (queue.empty()) {
            return; // No audio to inject
        }
        
        InjectionAudio& injection = queue.front();
        size_t frame_size = frame->datalen;
        size_t available_data = injection.pcm_data.size() - injection.position;
        
        if (available_data > 0) {
            // Determine how much data to copy (limit to frame size)
            size_t copy_size = std::min(frame_size, available_data);
            
            // Replace the frame data with our translated audio
            memcpy(frame->data, injection.pcm_data.data() + injection.position, copy_size);
            
            // If we copied less than the frame size, fill the rest with silence
            if (copy_size < frame_size) {
                memset(static_cast<uint8_t*>(frame->data) + copy_size, 0x00, frame_size - copy_size); // PCM silence (0x00)
            }
            
            injection.position += copy_size;
            
            // Log the injection
            static uint32_t injection_count = 0;
            if (++injection_count % 50 == 1) { // Log every 50th injection to avoid spam
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                 "mod_parlez: Injecting audio for participant %u: %zu bytes at position %zu/%zu\n",
                                 member_id, copy_size, injection.position, injection.pcm_data.size());
            }
            
            // Remove the injection audio if we've played it all
            if (injection.position >= injection.pcm_data.size()) {
                queue.pop();
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                 "mod_parlez: Finished injecting audio for participant %u (queue size: %zu)\n",
                                 member_id, queue.size());
            }
        }
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                         "mod_parlez: Exception injecting queued audio: %s\n", e.what());
    }
}

void AudioInjectionManager::removeParticipant(uint32_t member_id) {
    std::lock_guard<std::mutex> lock(injection_mutex_);
    injection_queues_.erase(member_id);
}

bool AudioInjectionManager::hasQueuedAudio(uint32_t member_id) const {
    std::lock_guard<std::mutex> lock(injection_mutex_);
    auto it = injection_queues_.find(member_id);
    return it != injection_queues_.end() && !it->second.empty();
}

size_t AudioInjectionManager::getQueueSize(uint32_t member_id) const {
    std::lock_guard<std::mutex> lock(injection_mutex_);
    auto it = injection_queues_.find(member_id);
    return it != injection_queues_.end() ? it->second.size() : 0;
}
