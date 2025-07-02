#include "audio_resampler.h"
#include <switch.h>
#include <algorithm>
#include <cstring>

AudioResampler::AudioResampler() {
    // Reserve initial buffer space
    resampled_buffer_.reserve(8192); // Should handle most audio chunks
}

const int16_t* AudioResampler::resampleTo16kHz(const int16_t* input_pcm, 
                                               size_t input_samples, 
                                               uint32_t input_sample_rate,
                                               size_t* output_samples) {
    if (!input_pcm || input_samples == 0 || !output_samples) {
        if (output_samples) *output_samples = 0;
        return nullptr;
    }
    
    // Fast path: No resampling needed for 16kHz input
    if (input_sample_rate == 16000) {
        *output_samples = input_samples;
        return input_pcm;  // Return input directly - no copy needed
    }
    
    size_t required_output_samples;
    
    // Calculate required output buffer size based on input sample rate
    if (input_sample_rate == 8000) {
        // Upsample 8kHz -> 16kHz (2:1 ratio)
        required_output_samples = input_samples * 2;
    } else if (input_sample_rate == 48000) {
        // Downsample 48kHz -> 16kHz (3:1 ratio)
        required_output_samples = input_samples / 3;
    } else {
        // Unsupported sample rate - log warning and pass through
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "AudioResampler: Unsupported sample rate %u Hz - passing through\n", 
                         input_sample_rate);
        required_output_samples = input_samples;
    }
    
    // Ensure buffer is large enough
    if (resampled_buffer_.size() < required_output_samples) {
        resampled_buffer_.resize(required_output_samples);
    }
    
    // Perform resampling based on input sample rate
    if (input_sample_rate == 8000) {
        resample8to16(input_pcm, resampled_buffer_.data(), input_samples);
    } else if (input_sample_rate == 48000) {
        resample48to16(input_pcm, resampled_buffer_.data(), input_samples);
    } else {
        // Pass through unsupported rates
        memcpy(resampled_buffer_.data(), input_pcm, input_samples * sizeof(int16_t));
    }
    
    *output_samples = required_output_samples;
    return resampled_buffer_.data();
}

const int16_t* AudioResampler::resampleFrom16kHz(const int16_t* input_pcm, 
                                                 size_t input_samples, 
                                                 uint32_t target_sample_rate,
                                                 size_t* output_samples) {
    if (!input_pcm || input_samples == 0 || !output_samples) {
        if (output_samples) *output_samples = 0;
        return nullptr;
    }
    
    size_t required_output_samples;
    
    // Fast path: No resampling needed for 16kHz target
    if (target_sample_rate == 16000) {
        *output_samples = input_samples;
        return input_pcm;  // Return input directly - no copy needed
    }
    
    // Calculate required output buffer size based on target sample rate
    if (target_sample_rate == 8000) {
        // Downsample 16kHz -> 8kHz (2:1 ratio)
        required_output_samples = input_samples / 2;
    } else {
        // Unsupported sample rate - log warning and pass through
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                         "AudioResampler: Unsupported target sample rate %u Hz - passing through\n", 
                         target_sample_rate);
        required_output_samples = input_samples;
    }
    
    // Ensure buffer is large enough
    if (resampled_buffer_.size() < required_output_samples) {
        resampled_buffer_.resize(required_output_samples);
    }
    
    // Perform resampling based on target sample rate
    if (target_sample_rate == 8000) {
        resample16to8(input_pcm, resampled_buffer_.data(), input_samples);
    } else {
        // Pass through unsupported rates
        memcpy(resampled_buffer_.data(), input_pcm, input_samples * sizeof(int16_t));
    }
    
    *output_samples = required_output_samples;
    return resampled_buffer_.data();
}

bool AudioResampler::isSampleRateSupported(uint32_t sample_rate) const {
    return (sample_rate == 8000 || sample_rate == 16000 || sample_rate == 48000);
}

bool AudioResampler::isTargetSampleRateSupported(uint32_t target_sample_rate) const {
    return (target_sample_rate == 8000 || target_sample_rate == 16000);
}

void AudioResampler::resample8to16(const int16_t* input, int16_t* output, size_t input_samples) {
    // Simple linear interpolation upsampling from 8kHz to 16kHz
    for (size_t i = 0; i < input_samples; i++) {
        output[i * 2] = input[i];
        
        // Interpolate between current and next sample
        if (i + 1 < input_samples) {
            output[i * 2 + 1] = (input[i] + input[i + 1]) / 2;
        } else {
            // Last sample - just duplicate
            output[i * 2 + 1] = input[i];
        }
    }
}

void AudioResampler::resample48to16(const int16_t* input, int16_t* output, size_t input_samples) {
    // 3-tap averaging filter + decimation to reduce aliasing
    size_t output_samples = input_samples / 3;
    for (size_t i = 0; i < output_samples; ++i) {
        size_t base_idx = i * 3;
        if (base_idx + 2 < input_samples) {
            // Average 3 samples to reduce aliasing
            int32_t sum = static_cast<int32_t>(input[base_idx]) + 
                         input[base_idx + 1] + input[base_idx + 2];
            output[i] = static_cast<int16_t>(sum / 3);
        } else {
            output[i] = input[base_idx];
        }
    }
}

void AudioResampler::resample16to8(const int16_t* input, int16_t* output, size_t input_samples) {
    // Simple decimation from 16kHz to 8kHz (take every other sample)
    size_t output_samples = input_samples / 2;
    for (size_t i = 0; i < output_samples; i++) {
        output[i] = input[i * 2];
    }
}
