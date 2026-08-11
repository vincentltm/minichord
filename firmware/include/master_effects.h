#pragma once

#include <Arduino.h>
#include <Audio.h>
#include "effect_pt2399_delay.h"
#include "effect_platervbstereo.h"

// Forward declarations of audio objects from audio_definition.h
extern AudioEffectPT2399Delay   pt2399_delay;
extern AudioEffectPlateReverb   main_reverb;
extern AudioMixer4              reverb_send_l;
extern AudioMixer4              reverb_send_r;
extern int16_t                  current_sysex_parameters[];

class MasterEffects {
public:
    MasterEffects() 
        : delay_time_ms_(400.0f)
        , delay_feedback_(0.0f)
        , delay_mix_(0.0f)
        , reverb_amount_(0.35f)
        , target_delay_time_ms_(400.0f)
        , target_delay_feedback_(0.0f)
        , target_delay_mix_(0.0f)
        , target_reverb_amount_(0.35f)
        , needs_update_(false)
    {}

    // Initialize audio objects with safe defaults
    void init() {
        pt2399_delay.clear();
        main_reverb.clear();
        pt2399_delay.setDelayTime(400.0f);
        pt2399_delay.setFeedback(0.0f);
        pt2399_delay.setMix(0.0f);
        pt2399_delay.setCrossFeedback(0.15f);

        reverb_amount_ = 0.35f;
        target_reverb_amount_ = 0.35f;
        applyReverb();
    }

    // Set delay time in ms (50ms to 3000ms)
    // Uses PT2399 clock-down downsampling at longer times for analog warmth & pitch wobble
    void setDelayTime(float normalizedValue) {
        float ms = 50.0f + normalizedValue * normalizedValue * 2950.0f;
        target_delay_time_ms_ = ms;
        current_sysex_parameters[240] = (int16_t)(normalizedValue * 100.0f);
        needs_update_ = true;
    }

    // Combined feedback + mix control (0.0 to 1.0)
    void setDelayFeedbackMix(float normalizedValue) {
        target_delay_feedback_ = normalizedValue * 0.92f;
        target_delay_mix_ = normalizedValue;
        current_sysex_parameters[241] = (int16_t)(normalizedValue * 100.0f);
        needs_update_ = true;
    }

    // Reverb amount (0.0 to 1.0)
    void setReverbAmount(float normalizedValue) {
        target_reverb_amount_ = normalizedValue;
        current_sysex_parameters[242] = (int16_t)(normalizedValue * 100.0f);
        needs_update_ = true;
    }

    // Smooth parameter update loop
    void update() {
        if (!needs_update_) return;

        const float smooth = 0.08f;
        bool still_moving = false;

        float dt = target_delay_time_ms_ - delay_time_ms_;
        if (fabsf(dt) > 0.5f) {
            delay_time_ms_ += dt * smooth;
            still_moving = true;
        } else {
            delay_time_ms_ = target_delay_time_ms_;
        }

        float df = target_delay_feedback_ - delay_feedback_;
        if (fabsf(df) > 0.001f) {
            delay_feedback_ += df * smooth;
            still_moving = true;
        } else {
            delay_feedback_ = target_delay_feedback_;
        }

        float dm = target_delay_mix_ - delay_mix_;
        if (fabsf(dm) > 0.001f) {
            delay_mix_ += dm * smooth;
            still_moving = true;
        } else {
            delay_mix_ = target_delay_mix_;
        }

        float dr = target_reverb_amount_ - reverb_amount_;
        if (fabsf(dr) > 0.001f) {
            reverb_amount_ += dr * smooth;
            still_moving = true;
        } else {
            reverb_amount_ = target_reverb_amount_;
        }

        if (!still_moving) needs_update_ = false;

        applyDelay();
        applyReverb();
    }

    float getDelayTimeMs() const { return delay_time_ms_; }
    float getDelayFeedback() const { return delay_feedback_; }
    float getDelayMix() const { return delay_mix_; }
    float getReverbAmount() const { return reverb_amount_; }

private:
    void applyDelay() {
        AudioNoInterrupts();
        pt2399_delay.setDelayTime(delay_time_ms_);
        pt2399_delay.setFeedback(delay_feedback_);
        pt2399_delay.setMix(delay_mix_);
        pt2399_delay.setCrossFeedback(delay_feedback_ * 0.15f);
        AudioInterrupts();
    }

    void applyReverb() {
        AudioNoInterrupts();
        float send = reverb_amount_;
        reverb_send_l.gain(0, send);
        reverb_send_r.gain(0, send);

        float size = 0.3f + reverb_amount_ * 0.65f;
        main_reverb.size(size);
        main_reverb.hidamp((1.0f - reverb_amount_) * 0.5f);
        AudioInterrupts();
    }

    float delay_time_ms_;
    float delay_feedback_;
    float delay_mix_;
    float reverb_amount_;

    float target_delay_time_ms_;
    float target_delay_feedback_;
    float target_delay_mix_;
    float target_reverb_amount_;

    bool needs_update_;
};
