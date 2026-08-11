#pragma once

#include <Arduino.h>
#include <Audio.h>

// PT2399-style downsampled stereo delay effect with zero zipper-noise per-sample smoothing.
// Extends delay time up to 3.0 seconds (3000ms) with variable clock-down downsampling,
// PT2399 warm analog lowpass filtering, and tape-style pitch sweeps.
class AudioEffectPT2399Delay : public AudioStream {
public:
    AudioEffectPT2399Delay();
    virtual void update();

    // Clear delay memory buffers and filter states
    void clear();

    // Delay time in ms (50ms to 3000ms)
    void setDelayTime(float ms);

    // Feedback (0.0 to 0.95)
    void setFeedback(float fb);

    // Dry/Wet Mix (0.0 = 100% dry, 1.0 = 100% wet)
    void setMix(float mix);

    // Cross-feedback ping-pong bleed (0.0 to 0.4)
    void setCrossFeedback(float xfb);

private:
    audio_block_t *inputQueueArray[2];

    float target_delay_ms_;
    float smooth_delay_ms_;
    float smooth_downsample_;
    
    float feedback_;
    float mix_;
    float cross_feedback_;

    float write_pos_f_;

    // One-pole anti-aliasing / PT2399 warm lowpass filters
    float lp_in_l_;
    float lp_in_r_;
    float lp_fb_l_;
    float lp_fb_r_;
};
