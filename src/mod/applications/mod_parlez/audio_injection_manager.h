#ifndef AUDIO_INJECTION_MANAGER_H
#define AUDIO_INJECTION_MANAGER_H

#include <switch.h>
#include <vector>
#include <queue>
#include <map>
#include <mutex>
#include <cstdint>

struct InjectionAudio {
    std::vector<uint8_t> pcm_data;  // Actually PCM data, naming kept for compatibility
    size_t position = 0;  // Current playback position
    uint32_t timestamp;
};

class AudioInjectionManager {
public:
    AudioInjectionManager();
    ~AudioInjectionManager();
    
    // Queue audio for injection to a specific participant
    void queueAudioForInjection(uint32_t member_id, const std::vector<uint8_t>& pcm_data);
    
    // Inject queued audio into a frame for a specific participant
    void injectQueuedAudio(uint32_t member_id, switch_frame_t* frame);
    
    // Remove all queued audio for a participant
    void removeParticipant(uint32_t member_id);
    
    // Check if there's queued audio for a participant
    bool hasQueuedAudio(uint32_t member_id) const;
    
    // Get queue size for a participant
    size_t getQueueSize(uint32_t member_id) const;
    
private:
    mutable std::mutex injection_mutex_;
    std::map<uint32_t, std::queue<InjectionAudio>> injection_queues_;  // Per-participant queues
};

#endif // AUDIO_INJECTION_MANAGER_H
