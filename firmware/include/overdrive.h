#pragma once
#include <cmath>

namespace minichord {

// Soft-clip overdrive with automatic volume compensation.
// Drive curve: amount = 0.2 + input * 0.3
// Volume comp: gain = (1.0 - input * 0.6)^2
// This prevents loudness spikes as drive increases.
class Overdrive {
public:
    Overdrive() : drive_(0.2f), volume_(1.0f), pre_gain_(1.8f) {}
    
    // Set drive amount (0.0 = clean, 1.0 = maximum drive)
    void SetDrive(float value) {
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        drive_ = 0.2f + value * 0.3f;
        volume_ = (1.0f - value * 0.6f) * (1.0f - value * 0.6f);
        pre_gain_ = 1.0f + drive_ * 4.0f;
    }
    
    // Process a single audio sample through the overdrive
    float Process(float sample) const {
        sample *= pre_gain_;
        // Cubic soft-clip waveshaping
        if (sample > 1.0f) sample = 1.0f;
        else if (sample < -1.0f) sample = -1.0f;
        else sample = sample * (1.5f - 0.5f * sample * sample);
        return sample * volume_;
    }
    
    float drive() const { return drive_; }
    float volume() const { return volume_; }

private:
    float drive_;
    float volume_;
    float pre_gain_;
};

// Soft limiter preventing hard digital clipping on the final output.
// Uses the same cubic soft-clip curve.
inline float SoftLimit(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

// Saturate a float to int16_t range
inline int16_t SaturateFloat16(float x) {
    int32_t i = static_cast<int32_t>(x);
    if (i > 32767) return 32767;
    if (i < -32768) return -32768;
    return static_cast<int16_t>(i);
}

}  // namespace minichord
