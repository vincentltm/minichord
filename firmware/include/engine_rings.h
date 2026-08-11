#pragma once

#include <Arduino.h>
#include <Audio.h>
#include <AudioStream.h>
#include "engine_interface.h"
#include "rings/dsp/part.h"
#include "rings/dsp/fx/reverb.h"
#include "rings/dsp/patch.h"
#include "master_effects.h"

extern MasterEffects masterEffects;

// Forward declaration
class EngineRings;

class AudioStreamRings : public AudioStream {
public:
    AudioStreamRings(EngineRings* engine) : AudioStream(0, nullptr), engine_(engine), active_(false) {}
    virtual void update() override;
    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }

private:
    EngineRings* engine_;
    volatile bool active_;
};

// Per-model default parameter presets
struct ModelDefaults {
    float structure;
    float brightness;
    float damping;
    float position;
    int16_t touch_pressure_mode; // 0 = Off, 1 = Pluck Velocity Only, 2 = Full Pressure + Aftertouch
    bool harp_bowing_enabled;
};

class EngineRings : public MinichordEngine {
public:
    EngineRings();
    virtual ~EngineRings() = default;

    void init() override;
    void activate() override;
    void deactivate() override;
    
    void onChordChange(uint8_t rootNote, const uint8_t chord[7], bool sharp, bool maj, bool min, bool sev) override;
    void onChordRelease() override;
    void onHarpTouch(uint8_t padIndex, bool pressed, uint8_t noteValue, float pressure = 1.0f) override;
    void onPotChange(uint8_t potIndex, float normalizedValue, bool shifted, bool holdPressed) override;
    void onSequencerNote(uint8_t voiceIndex, uint8_t noteValue, float accent) override;
    void onHoldToggle(bool continuous) override;
    
    void processAudio(int16_t* outputL, int16_t* outputR, size_t blockSize) override {}
    
    void sendMidi() override;
    void applyParameter(uint16_t address, int16_t value) override;
    void getParameterDefaults(int16_t* dest, size_t size) override;
    
    const char* presetFilename(uint8_t bankIndex) override;
    const char* name() const override { return "Rings Dual"; }
    float ledHue() const override;

    // Model selection — independent chord and harp models
    void setModel(rings::ResonatorModel model);          // Sets BOTH chord+harp (for physical button)
    void setChordModel(rings::ResonatorModel model);     // Sets chord model only
    void setHarpModel(rings::ResonatorModel model);      // Sets harp model only
    
    void setSubModel(int32_t subModel) override {
        if (subModel >= 0) setModel(static_cast<rings::ResonatorModel>(subModel));
    }
    AudioStreamRings& getStream() { return stream_; }

    // Mutable Instruments Rings Dattorro Reverb
    rings::Reverb rings_reverb_;

    // Volume & Stereo Controls
    void setChordVolume(float vol) { chord_volume_ = vol; }
    void setHarpVolume(float vol) { harp_volume_ = vol; }
    void setStereoWidth(float width) { stereo_width_ = width; }

    // Arpeggiator / Rhythm pattern step trigger
    void arpTick();
    void triggerRhythmStep(uint8_t voiceIndex, uint8_t noteValue);

private:
    friend class AudioStreamRings;

    // Model-aware note clamping
    float clampNoteForModel(float note, rings::ResonatorModel model) const;
    
    // Apply per-model default parameters
    void applyModelDefaults(rings::ResonatorModel model, rings::Patch& patch);

    // Independent Dual DSP Resonators (Chord + Harp)
    rings::Part chord_part_;
    rings::Part harp_part_;
    AudioStreamRings stream_;
    
    rings::Patch chord_patch_;
    rings::Patch harp_patch_;
    
    rings::PerformanceState chord_perf_state_;
    rings::PerformanceState harp_perf_state_;
    
    // Independent models for chord and harp
    rings::ResonatorModel chordModel_;
    rings::ResonatorModel harpModel_;
    
    float chord_volume_;
    float harp_volume_;
    float stereo_width_;
    float reverb_mix_;
    int16_t overdrive_mode_;  // 0 = Off, 1 = Soft Tube, 2 = High-Gain Electric Guitar Amp
    int16_t touch_pressure_mode_; // 0 = Off (Fixed Pluck), 1 = Pluck Velocity Only, 2 = Full Pressure + Aftertouch
    bool harp_bowing_enabled_;   // false = Muted/Off, true = Active during hold/bowing

    // 4-Voice Polyphonic Panned Chord Queue
    float chord_notes_[4];
    volatile uint8_t chord_strum_counter_;
    
    // Chord strum spread (samples between each chord note trigger)
    uint16_t chord_strum_spread_samples_;
    uint16_t chord_strum_spread_counter_;

    // Model-switch guard — prevents ISR from processing during reconfiguration
    volatile bool switching_;
    
    // Bowing / Hold state
    bool bow_active_;
    bool continuous_hold_;
    bool is_chord_held_;
    // Elements-style flow exciter state for bowing
    float bow_particle_state_;
    float bow_filter_state_[2];  // SVF state (low, band)
    float chord_bow_gain_smooth_;
    float harp_bow_gain_smooth_;
    float vibrato_lfo_phase_;
    int16_t chord_voicing_spread_; // 0 = Closed Voicing, 1 = Wide Octave Spread
    float harp_base_position_;
    float harp_base_brightness_;
    
    // Harp note deduplication, pressure & per-pad held bowing tracking
    float harp_ringing_notes_[4];
    uint8_t harp_ringing_index_;
    float harp_pad_pressures_[12];
    uint32_t harp_pad_touch_time_[12];
    bool harp_pad_held_bowing_[12];
    volatile bool harp_held_bowing_active_;
    
    // Arpeggiator (timing via sample counting in audio ISR)
    bool arp_enabled_;
    float arp_bpm_;
    uint32_t arp_samples_per_step_;
    uint32_t arp_sample_counter_;
    uint8_t arp_step_;
    
    // Pot pickup / latching state for noise suppression and shift-jump prevention
    float prev_pot_val_[3];
    bool pot_latched_[3];
    bool prev_shift_state_;

    // Static per-model defaults
    static const ModelDefaults kModelDefaults[];
};
