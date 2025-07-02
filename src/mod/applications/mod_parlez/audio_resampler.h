#ifndef AUDIO_RESAMPLER_H
#define AUDIO_RESAMPLER_H

#include <vector>
#include <cstdint>
#include <cstddef>

/**
 * AudioResampler - Handles audio sample rate conversion for mod_parlez
 * Converts audio from various input sample rates to 16kHz for Azure Speech SDK
 */
class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler() = default;
    
    /**
     * Resample audio to 16kHz target rate
     * @param input_pcm Input PCM16 audio data
     * @param input_samples Number of input samples
     * @param input_sample_rate Input sample rate (8000, 16000, 48000, etc.)
     * @param output_samples Output parameter - number of output samples produced
     * @return Pointer to resampled PCM16 data (valid until next call)
     */
    const int16_t* resampleTo16kHz(const int16_t* input_pcm, 
                                   size_t input_samples, 
                                   uint32_t input_sample_rate,
                                   size_t* output_samples);
    
    /**
     * Resample audio from 16kHz to target rate
     * @param input_pcm Input PCM16 audio data at 16kHz
     * @param input_samples Number of input samples
     * @param target_sample_rate Target sample rate (8000, 16000, 48000, etc.)
     * @param output_samples Output parameter - number of output samples produced
     * @return Pointer to resampled PCM16 data (valid until next call)
     */
    const int16_t* resampleFrom16kHz(const int16_t* input_pcm, 
                                     size_t input_samples, 
                                     uint32_t target_sample_rate,
                                     size_t* output_samples);
    
    /**
     * Check if a sample rate is supported for resampling
     * @param sample_rate Sample rate to check
     * @return true if supported, false otherwise
     */
    bool isSampleRateSupported(uint32_t sample_rate) const;
    
    /**
     * Check if a target sample rate is supported for downsampling from 16kHz
     * @param target_sample_rate Target sample rate to check
     * @return true if supported, false otherwise
     */
    bool isTargetSampleRateSupported(uint32_t target_sample_rate) const;
    
    /**
     * Get the target sample rate (always 16kHz for Azure)
     * @return Target sample rate in Hz
     */
    static constexpr uint32_t getTargetSampleRate() { return 16000; }
    
private:
    // Resampling methods
    void resample8to16(const int16_t* input, int16_t* output, size_t input_samples);
    void resample48to16(const int16_t* input, int16_t* output, size_t input_samples);
    void resample16to8(const int16_t* input, int16_t* output, size_t input_samples);
    
    // Internal buffer for resampled audio
    std::vector<int16_t> resampled_buffer_;
    
    // Constants
    static const uint32_t TARGET_SAMPLE_RATE = 16000;
};

#endif // AUDIO_RESAMPLER_H
