#include "engine_rings.h"
#include "stmlib/utils/random.h"

DMAMEM static uint16_t reverb_buffer_[16384];

// Per-model default parameter presets — tuned so each model sounds distinct and vibrant
const ModelDefaults EngineRings::kModelDefaults[] = {
    // RESONATOR_MODEL_MODAL — bell-like, clear partials: pressure OFF, bowing OFF
    { 0.4f, 0.7f, 0.6f, 0.5f, 0, false },
    // RESONATOR_MODEL_SYMPATHETIC_STRING — sitar-like, buzzing resonance: velocity ON, bowing ON
    { 0.3f, 0.5f, 0.4f, 0.5f, 1, true },
    // RESONATOR_MODEL_STRING (Inharmonic) — piano/marimba/bowed cello: full pressure ON, bowing ON
    { 0.7f, 0.4f, 0.5f, 0.5f, 2, true },
    // RESONATOR_MODEL_FM_VOICE — clean, warm 4-op FM synth: velocity ON, bowing OFF
    { 0.50f, 0.65f, 0.60f, 0.50f, 1, false },
    // RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED
    { 0.3f, 0.5f, 0.4f, 0.5f, 1, true },
    // RESONATOR_MODEL_STRING_AND_REVERB
    { 0.7f, 0.4f, 0.5f, 0.5f, 2, true },
};

EngineRings::EngineRings() 
    : stream_(this), 
      chordModel_(rings::RESONATOR_MODEL_MODAL),
      harpModel_(rings::RESONATOR_MODEL_MODAL),
      chord_volume_(1.0f),
      harp_volume_(1.0f),
      stereo_width_(0.15f),
      reverb_mix_(0.2f),
      overdrive_mode_(2),
      touch_pressure_mode_(0),
      harp_bowing_enabled_(false),
      chord_strum_counter_(0),
      chord_strum_spread_samples_(0),
      chord_strum_spread_counter_(0),
      switching_(false),
      bow_active_(false),
      continuous_hold_(false),
      is_chord_held_(false),
      bow_particle_state_(0.0f),
      chord_bow_gain_smooth_(0.0f),
      harp_bow_gain_smooth_(0.0f),
      vibrato_lfo_phase_(0.0f),
      chord_voicing_spread_(1),
      harp_base_position_(0.5f),
      harp_base_brightness_(0.5f),
      harp_ringing_index_(0),
      arp_enabled_(false),
      arp_bpm_(120.0f),
      arp_samples_per_step_(0),
      arp_sample_counter_(0),
      arp_step_(0) {
    
    chord_patch_.structure = 0.5f;
    chord_patch_.brightness = 0.5f;
    chord_patch_.damping = 0.5f;
    chord_patch_.position = 0.5f;

    harp_patch_.structure = 0.5f;
    harp_patch_.brightness = 0.5f;
    harp_patch_.damping = 0.5f;
    harp_patch_.position = 0.5f;
    
    // Chord Resonator state
    chord_perf_state_.strum = false;
    chord_perf_state_.internal_exciter = true;
    chord_perf_state_.internal_strum = false;
    chord_perf_state_.internal_note = true;
    chord_perf_state_.tonic = 0.0f;
    chord_perf_state_.note = 60.0f;
    chord_perf_state_.fm = 0.0f;
    chord_perf_state_.chord = 0;

    // Harp Resonator state
    harp_perf_state_.strum = false;
    harp_perf_state_.internal_exciter = true;
    harp_perf_state_.internal_strum = false;
    harp_perf_state_.internal_note = true;
    harp_perf_state_.tonic = 0.0f;
    harp_perf_state_.note = 60.0f;
    harp_perf_state_.fm = 0.0f;
    harp_perf_state_.chord = 0;
    
    // Initialize bow filter state
    bow_filter_state_[0] = 0.0f;
    bow_filter_state_[1] = 0.0f;
    
    // Initialize pot pickup tracking
    prev_shift_state_ = false;
    for (int i = 0; i < 3; i++) {
        prev_pot_val_[i] = -1.0f;
        pot_latched_[i] = true;  // Latched by default so initial knob movements work immediately
    }
    
    // Initialize harp note & touch hold tracking
    harp_held_bowing_active_ = false;
    for (int i = 0; i < 12; i++) {
        harp_pad_pressures_[i] = 0.0f;
        harp_pad_touch_time_[i] = 0;
        harp_pad_held_bowing_[i] = false;
    }
    for (int i = 0; i < 4; i++) {
        harp_ringing_notes_[i] = -1.0f;
        chord_notes_[i] = 60.0f;
    }
}

void EngineRings::init() {
    AudioNoInterrupts();
    // Do NOT activate stream at init — prevents boot plonk.
    // Stream will be activated only when this engine is explicitly selected.
    stream_.setActive(false);
    
    // Zero DMAMEM resonator buffer to prevent garbage RAM noise on boot
    memset(reverb_buffer_, 0, sizeof(reverb_buffer_));
    
    // Initialize Chord Resonator (4 voices) and Harp Resonator (6 voices)
    chord_part_.Init(reverb_buffer_);
    harp_part_.Init(reverb_buffer_ + 8192);
    
    chord_part_.set_polyphony(4);
    harp_part_.set_polyphony(6);
    
    chordModel_ = rings::RESONATOR_MODEL_MODAL;
    harpModel_ = rings::RESONATOR_MODEL_MODAL;
    chord_part_.set_model(chordModel_);
    harp_part_.set_model(harpModel_);

    // Initialize Mutable Instruments Rings Dattorro Reverb in RAM1 (32768 words = 65KB)
    static uint16_t rings_dattorro_reverb_buffer[32768];
    memset(rings_dattorro_reverb_buffer, 0, sizeof(rings_dattorro_reverb_buffer));
    rings_reverb_.Init(rings_dattorro_reverb_buffer);
    rings_reverb_.Clear();
    // Disable internal Rings reverb — Master Effects Bus handles reverb exclusively for all engines
    rings_reverb_.set_amount(0.0f);
    rings_reverb_.set_input_gain(0.0f);

    AudioInterrupts();
}

void EngineRings::activate() {
    AudioNoInterrupts();
    if (chord_volume_ < 0.1f) chord_volume_ = 1.0f;
    if (harp_volume_ < 0.1f) harp_volume_ = 1.0f;
    chord_part_.set_polyphony(4);
    harp_part_.set_polyphony(6);

    // Clear reverb buffer and bowing smoothers on activation so switching modes starts in complete silence
    rings_reverb_.Clear();
    chord_bow_gain_smooth_ = 0.0f;
    harp_bow_gain_smooth_ = 0.0f;

    stream_.setActive(true);
    AudioInterrupts();
}

void EngineRings::deactivate() {
    AudioNoInterrupts();
    // Clear all bowing state so nothing leaks across mode switches or back to Stock
    bow_active_ = false;
    is_chord_held_ = false;
    chord_bow_gain_smooth_ = 0.0f;
    harp_bow_gain_smooth_ = 0.0f;
    harp_held_bowing_active_ = false;
    for (int i = 0; i < 12; i++) {
        harp_pad_touch_time_[i] = 0;
        harp_pad_held_bowing_[i] = false;
        harp_pad_pressures_[i] = 0.0f;
    }
    chord_perf_state_.strum = false;
    harp_perf_state_.strum = false;
    chord_strum_counter_ = 0;
    stream_.setActive(false);
    AudioInterrupts();
}

// Model-aware note clamping — prevents broken sounds at pitch extremes
float EngineRings::clampNoteForModel(float note, rings::ResonatorModel model) const {
    float minNote, maxNote;
    
    switch (model) {
        case rings::RESONATOR_MODEL_SYMPATHETIC_STRING:
        case rings::RESONATOR_MODEL_STRING:
        case rings::RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED:
        case rings::RESONATOR_MODEL_STRING_AND_REVERB:
            minNote = 36.0f;
            maxNote = 108.0f;
            break;
        case rings::RESONATOR_MODEL_MODAL:
            minNote = 33.0f;
            maxNote = 108.0f;
            break;
        case rings::RESONATOR_MODEL_FM_VOICE:
            minNote = 36.0f;
            maxNote = 108.0f;
            break;
        default:
            minNote = 36.0f;
            maxNote = 108.0f;
            break;
    }
    
    return max(minNote, min(maxNote, note));
}

void EngineRings::applyModelDefaults(rings::ResonatorModel model, rings::Patch& patch) {
    int idx = static_cast<int>(model);
    if (idx >= 0 && idx < static_cast<int>(rings::RESONATOR_MODEL_LAST)) {
        patch.structure = kModelDefaults[idx].structure;
        patch.brightness = kModelDefaults[idx].brightness;
        patch.damping = kModelDefaults[idx].damping;
        patch.position = kModelDefaults[idx].position;
        harp_base_position_ = kModelDefaults[idx].position;
        harp_base_brightness_ = kModelDefaults[idx].brightness;
        touch_pressure_mode_ = kModelDefaults[idx].touch_pressure_mode;
        harp_bowing_enabled_ = kModelDefaults[idx].harp_bowing_enabled;
    }
}

void EngineRings::onHoldToggle(bool continuous) {
    continuous_hold_ = continuous;
    bow_active_ = continuous || is_chord_held_;
    // NOTE: Do NOT override damping here — Knob 3 owns damping at all times.
    // Hold mode only activates the bow noise exciter; the resonator's decay
    // is intentionally left at whatever the user has set on Knob 3.
}

void EngineRings::onChordChange(uint8_t rootNote, const uint8_t chord[7], bool sharp, bool maj, bool min, bool sev) {
    is_chord_held_ = true;
    bow_active_ = true;  // Activate continuous Elements bow exciter while chord is held
    
    // Populate exact MIDI pitches (C2 bass octave = 36.0f + relative chord note)
    for (int i = 0; i < 4; i++) {
        float pitch = static_cast<float>(chord[i]);
        float rawNote = (pitch >= 36.0f) ? pitch : (36.0f + pitch);
        chord_notes_[i] = clampNoteForModel(rawNote, chordModel_);
    }

    // Wide Octave Spread: drop Bass voice down an octave, push Soprano voice up an octave
    if (chord_voicing_spread_ > 0) {
        chord_notes_[0] = clampNoteForModel(chord_notes_[0] - 12.0f, chordModel_);
        chord_notes_[3] = clampNoteForModel(chord_notes_[3] + 12.0f, chordModel_);
    }

    // Direct mapping of button flags to Rings chord structure index:
    // 0=Octave, 1=Fifth, 2=Minor, 3=Major, 4=Sus4/Dim, 5=Minor7th, 6=Major7th, 7=Dominant7th, 9=Aug
    if (maj && !min && !sev) {
        chord_perf_state_.chord = 3;  // Major
    } else if (!maj && min && !sev) {
        chord_perf_state_.chord = 2;  // Minor
    } else if (!maj && !min && sev) {
        chord_perf_state_.chord = 7;  // Dominant 7th
    } else if (maj && !min && sev) {
        chord_perf_state_.chord = 6;  // Major 7th
    } else if (!maj && min && sev) {
        chord_perf_state_.chord = 5;  // Minor 7th
    } else if (maj && min && !sev) {
        chord_perf_state_.chord = 4;  // Diminished / Sus4
    } else if (maj && min && sev) {
        chord_perf_state_.chord = 9;  // Augmented
    } else {
        chord_perf_state_.chord = 3;  // Major fallback
    }

    extern bool rythm_mode;
    if (chord_volume_ > 0.01f && !rythm_mode) {
        chord_strum_counter_ = 4;  // Trigger 4-voice polyphonic chord strum queue
        chord_strum_spread_counter_ = 0;
    }
}

void EngineRings::onChordRelease() {
    is_chord_held_ = false;
    if (!continuous_hold_) {
        bow_active_ = false;  // Silence continuous bowing exciter on chord release ONLY IF Hold is off
    }
}

void EngineRings::onHarpTouch(uint8_t padIndex, bool pressed, uint8_t noteValue, float pressure) {
    if (padIndex >= 12) return;

    harp_pad_pressures_[padIndex] = pressed ? pressure : 0.0f;

    if (pressed) {
        uint32_t now = millis();

        // 1. Initial Touch Moment: trigger clean articulate acoustic pluck using live Knob 3 decay damping
        if (harp_pad_touch_time_[padIndex] == 0) {
            harp_pad_touch_time_[padIndex] = now;
            harp_pad_held_bowing_[padIndex] = false;

            float clampedNote = clampNoteForModel(48.0f + static_cast<float>(noteValue), harpModel_);
            harp_perf_state_.note = clampedNote;

            // Preserve live Position knob setting with slight pad tint (+/- 0.08 across pads)
            float padOffset = (static_cast<float>(padIndex) - 5.5f) * 0.015f;
            harp_patch_.position = max(0.05f, min(0.95f, harp_base_position_ + padOffset));

            // Optional touch pressure brightness modulation based on LIVE brightness knob setting
            if (touch_pressure_mode_ > 0) {
                float pressNorm = min(1.0f, max(0.0f, pressure));
                harp_patch_.brightness = min(0.95f, harp_base_brightness_ + pressNorm * 0.15f);
            } else {
                harp_patch_.brightness = harp_base_brightness_;
            }

            // Excite dedicated Harp Resonator with clean articulate internal pluck
            if (harp_volume_ > 0.01f) {
                harp_perf_state_.strum = true;
                harp_ringing_notes_[harp_ringing_index_] = clampedNote;
                harp_ringing_index_ = (harp_ringing_index_ + 1) % 4;
            }
        } 
        // 2. Press & Hold Moment: if finger remains held past 180ms, activate bowed sustain.
        else {
            uint32_t holdDuration = now - harp_pad_touch_time_[padIndex];
            if (holdDuration > 180 && harp_bowing_enabled_) {
                harp_pad_held_bowing_[padIndex] = true;
            }
        }
    } else {
        // Touch Released: reset hold timer. Bowing gain smoother fades out naturally.
        harp_pad_touch_time_[padIndex] = 0;
        harp_pad_held_bowing_[padIndex] = false;
    }

    // Update global held bowing status
    bool anyHeldBowing = false;
    for (int i = 0; i < 12; i++) {
        if (harp_pad_held_bowing_[i]) {
            anyHeldBowing = true;
            break;
        }
    }
    harp_held_bowing_active_ = anyHeldBowing;
}

void EngineRings::onPotChange(uint8_t potIndex, float normalizedValue, bool shifted, bool holdPressed) {
    // When hold is pressed, knobs control the generative sequencer (handled by main.cpp)
    // The engine ignores pot changes in this state
    if (holdPressed) return;

    if (potIndex == 0) {
        if (!shifted) {
            chord_volume_ = normalizedValue;  // Knob 1 = Chord Volume
        } else {
            chord_patch_.structure = normalizedValue;
            harp_patch_.structure = normalizedValue;
        }
    } else if (potIndex == 1) {
        if (!shifted) {
            harp_volume_ = normalizedValue;   // Knob 2 = Harp Volume
        } else {
            chord_patch_.position = normalizedValue;
            harp_patch_.position = normalizedValue;
            harp_base_position_ = normalizedValue;
        }
    } else if (potIndex == 2) {
        if (!shifted) {
            // Knob 3 Unshifted = Resonator Damping (Decay time). Clamp to 0.96f max to prevent infinite SVF ringing / stuck notes!
            float d = min(0.96f, normalizedValue);
            chord_patch_.damping = d;
            harp_patch_.damping = d;
        } else {
            // Knob 3 Shifted = Resonator Brightness (Cutoff)
            chord_patch_.brightness = normalizedValue;
            harp_patch_.brightness = normalizedValue;
            harp_base_brightness_ = normalizedValue;
        }
    }
}

// Generative sequencer note trigger — plays a chord voice through chord_part_
void EngineRings::onSequencerNote(uint8_t voiceIndex, uint8_t noteValue, float accent) {
    if (chord_volume_ < 0.01f) return;

    uint8_t idx = (voiceIndex < 4) ? voiceIndex : (voiceIndex % 4);
    float note;
    if (noteValue == 0) {
        note = chord_notes_[idx];
    } else {
        float rawPitch = static_cast<float>(noteValue);
        note = (rawPitch >= 36.0f) ? rawPitch : (36.0f + rawPitch);
    }

    float clampedNote = clampNoteForModel(note, chordModel_);
    if (chord_voicing_spread_ > 0) {
        if (idx % 4 == 0) clampedNote = clampNoteForModel(clampedNote - 12.0f, chordModel_);
        else if (idx % 4 == 3) clampedNote = clampNoteForModel(clampedNote + 12.0f, chordModel_);
    }

    chord_perf_state_.note = clampedNote;
    chord_perf_state_.strum = true;
}



void EngineRings::sendMidi() {
}

void EngineRings::applyParameter(uint16_t address, int16_t value) {
    float norm = value / 1000.0f;
    switch(address) {
        // Independent chord/harp model selection
        case 236: setChordModel(static_cast<rings::ResonatorModel>(max(0, value - 1))); break;
        case 246: setHarpModel(static_cast<rings::ResonatorModel>(max(0, value - 1))); break;
        
        // Shared params (knobs control both, kept for backward compat)
        case 247: chord_patch_.structure = norm; harp_patch_.structure = norm; break;
        case 248: chord_patch_.brightness = norm; harp_patch_.brightness = norm; harp_base_brightness_ = norm; break;
        case 249: { float d = min(0.96f, norm); chord_patch_.damping = d; harp_patch_.damping = d; break; }
        case 250: chord_patch_.position = norm; harp_patch_.position = norm; harp_base_position_ = norm; break;
        
        // Independent chord patch params
        case 256: chord_patch_.structure = norm; break;
        case 257: chord_patch_.brightness = norm; break;
        case 258: chord_patch_.damping = min(0.96f, norm); break;
        case 259: chord_patch_.position = norm; break;
        
        // Independent harp patch params
        case 260: harp_patch_.structure = norm; break;
        case 261: harp_patch_.brightness = norm; harp_base_brightness_ = norm; break;
        case 262: harp_patch_.damping = min(0.96f, norm); break;
        case 263: harp_patch_.position = norm; harp_base_position_ = norm; break;
        
        // Volume from stock pot objects (0..100 scale)
        case 2: harp_volume_ = value / 100.0f; break;
        case 3: chord_volume_ = value / 100.0f; break;
        
        case 251: {
            reverb_mix_ = norm;
            masterEffects.setReverbAmount(norm);
            break;
        }
        case 252: {
            AudioNoInterrupts();
            chord_part_.set_polyphony(min((int)value, 4));
            harp_part_.set_polyphony(min((int)value, 6));
            AudioInterrupts();
            break;
        }
        case 253: stereo_width_ = norm; break;
        case 244: chord_volume_ = (value == 0) ? 1.0f : norm; break;
        case 245: harp_volume_ = (value == 0) ? 1.0f : norm; break;
        
        // Arpeggiator
        case 254: {
            arp_enabled_ = (value > 0);
            if (arp_enabled_) {
                arp_step_ = 0;
                arp_sample_counter_ = 0;
                arp_samples_per_step_ = static_cast<uint32_t>(44100.0f * 60.0f / (arp_bpm_ * 4.0f));
            }
            break;
        }
        case 255: {
            arp_bpm_ = max(40.0f, min(240.0f, static_cast<float>(value)));
            arp_samples_per_step_ = static_cast<uint32_t>(44100.0f * 60.0f / (arp_bpm_ * 4.0f));
            break;
        }
        case 264: {
            overdrive_mode_ = max(0, min(3, (int)value));
            break;
        }
        case 265: {
            touch_pressure_mode_ = max(0, min(2, (int)value));
            break;
        }
        case 266: {
            harp_bowing_enabled_ = (value > 0);
            break;
        }
        case 267: {
            chord_voicing_spread_ = (value > 0) ? 1 : 0;
            break;
        }
    }
}

void EngineRings::getParameterDefaults(int16_t* dest, size_t size) {
    if (size > 236) dest[236] = 0;
    if (size > 246) dest[246] = 0;
    if (size > 247) dest[247] = 500;
    if (size > 248) dest[248] = 500;
    if (size > 249) dest[249] = 500;
    if (size > 250) dest[250] = 500;
    if (size > 251) dest[251] = 200;
    if (size > 252) dest[252] = 4;
    if (size > 253) dest[253] = 700;
    if (size > 244) dest[244] = 1000;
    if (size > 245) dest[245] = 1000;
    if (size > 254) dest[254] = 0;
    if (size > 255) dest[255] = 120;
    if (size > 264) dest[264] = 2; // Electric Guitar Amp overdrive enabled by default!
    if (size > 265) dest[265] = 0; // Touch pressure mode default (Off)
    if (size > 266) dest[266] = 0; // Harp bowing default (Disabled)
    if (size > 267) dest[267] = 1; // Wide Octave Voicing Spread enabled by default
    // Independent chord params
    if (size > 256) dest[256] = 500;
    if (size > 257) dest[257] = 500;
    if (size > 258) dest[258] = 500;
    if (size > 259) dest[259] = 500;
    // Independent harp params
    if (size > 260) dest[260] = 500;
    if (size > 261) dest[261] = 500;
    if (size > 262) dest[262] = 500;
    if (size > 263) dest[263] = 500;
}

const char* EngineRings::presetFilename(uint8_t bankIndex) {
    static const char* names[] = {
        "pm_a.txt", "pm_b.txt", "pm_c.txt", "pm_d.txt",
        "pm_e.txt", "pm_f.txt", "pm_g.txt", "pm_h.txt",
        "pm_i.txt", "pm_j.txt", "pm_k.txt", "pm_l.txt"
    };
    return names[bankIndex % 12];
}

float EngineRings::ledHue() const {
    switch (chordModel_) {
        case rings::RESONATOR_MODEL_MODAL: return 120.0f;  // Green
        case rings::RESONATOR_MODEL_SYMPATHETIC_STRING: return 60.0f;  // Yellow
        case rings::RESONATOR_MODEL_STRING: return 0.0f;  // Red
        case rings::RESONATOR_MODEL_FM_VOICE: return 180.0f;  // Cyan
        default: return 120.0f;
    }
}

// Set BOTH chord and harp to the same model (for physical button cycling)
void EngineRings::setModel(rings::ResonatorModel model) {
    switching_ = true;
    AudioNoInterrupts();
    
    bool wasActive = stream_.isActive();
    stream_.setActive(false);
    
    // Clear all transient state so the new model starts completely clean
    chord_perf_state_.strum = false;
    harp_perf_state_.strum = false;
    chord_strum_counter_ = 0;
    chord_bow_gain_smooth_ = 0.0f;
    harp_bow_gain_smooth_ = 0.0f;
    bow_filter_state_[0] = 0.0f;
    bow_filter_state_[1] = 0.0f;
    harp_held_bowing_active_ = false;
    for (int i = 0; i < 12; i++) {
        harp_pad_touch_time_[i] = 0;
        harp_pad_held_bowing_[i] = false;
    }
    
    chordModel_ = model;
    harpModel_ = model;
    chord_part_.set_model(model);
    harp_part_.set_model(model);
    
    applyModelDefaults(model, chord_patch_);
    applyModelDefaults(model, harp_patch_);
    
    // Run TWO silent Process() blocks to flush Rings' internal polyphonic voice allocator
    float silent_in[rings::kMaxBlockSize] = {0};
    float silent_out[rings::kMaxBlockSize] = {0};
    float silent_aux[rings::kMaxBlockSize] = {0};
    
    rings::PerformanceState silent_state = chord_perf_state_;
    silent_state.strum = false;
    chord_part_.Process(silent_state, chord_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    harp_part_.Process(silent_state, harp_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    chord_part_.Process(silent_state, chord_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    harp_part_.Process(silent_state, harp_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    
    stream_.setActive(wasActive);
    AudioInterrupts();
    switching_ = false;
}

// Set chord model independently
void EngineRings::setChordModel(rings::ResonatorModel model) {
    switching_ = true;
    AudioNoInterrupts();
    
    bool wasActive = stream_.isActive();
    stream_.setActive(false);
    
    chord_perf_state_.strum = false;
    chord_strum_counter_ = 0;
    chord_bow_gain_smooth_ = 0.0f;
    bow_filter_state_[0] = 0.0f;
    bow_filter_state_[1] = 0.0f;
    
    chordModel_ = model;
    chord_part_.set_model(model);
    applyModelDefaults(model, chord_patch_);
    
    float silent_in[rings::kMaxBlockSize] = {0};
    float silent_out[rings::kMaxBlockSize] = {0};
    float silent_aux[rings::kMaxBlockSize] = {0};
    rings::PerformanceState silent_state = chord_perf_state_;
    silent_state.strum = false;
    chord_part_.Process(silent_state, chord_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    chord_part_.Process(silent_state, chord_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    
    stream_.setActive(wasActive);
    AudioInterrupts();
    switching_ = false;
}

// Set harp model independently
void EngineRings::setHarpModel(rings::ResonatorModel model) {
    switching_ = true;
    AudioNoInterrupts();
    
    bool wasActive = stream_.isActive();
    stream_.setActive(false);
    
    harp_perf_state_.strum = false;
    harp_bow_gain_smooth_ = 0.0f;
    harp_held_bowing_active_ = false;
    for (int i = 0; i < 12; i++) {
        harp_pad_touch_time_[i] = 0;
        harp_pad_held_bowing_[i] = false;
    }
    
    harpModel_ = model;
    harp_part_.set_model(model);
    applyModelDefaults(model, harp_patch_);
    
    for (int i = 0; i < 4; i++) harp_ringing_notes_[i] = -1.0f;
    harp_ringing_index_ = 0;
    
    float silent_in[rings::kMaxBlockSize] = {0};
    float silent_out[rings::kMaxBlockSize] = {0};
    float silent_aux[rings::kMaxBlockSize] = {0};
    rings::PerformanceState silent_state = harp_perf_state_;
    silent_state.strum = false;
    harp_part_.Process(silent_state, harp_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    harp_part_.Process(silent_state, harp_patch_, silent_in, silent_out, silent_aux, rings::kMaxBlockSize);
    
    stream_.setActive(wasActive);
    AudioInterrupts();
    switching_ = false;
}

// Arpeggiator tick — plays chord notes through chord_part_ ONLY
void EngineRings::arpTick() {
    if (!arp_enabled_ || chord_volume_ < 0.01f) return;
    
    // Cycle through active chord notes on the chord part
    float note = chord_notes_[arp_step_ % 4];
    chord_perf_state_.note = clampNoteForModel(note, chordModel_);
    chord_perf_state_.strum = true;
    
    arp_step_ = (arp_step_ + 1) % 4;
}

// Rhythm pattern step trigger — plays ALL rhythm steps through chord_part_ ONLY
void EngineRings::triggerRhythmStep(uint8_t voiceIndex, uint8_t noteValue) {
    if (chord_volume_ < 0.01f || noteValue == 0) return;

    float rawPitch = static_cast<float>(noteValue);
    float note = (rawPitch >= 36.0f) ? rawPitch : (36.0f + rawPitch);

    uint8_t idx = (voiceIndex < 4) ? voiceIndex : (voiceIndex - 3);
    float clampedNote = clampNoteForModel(note, chordModel_);
    if (chord_voicing_spread_ > 0) {
        if (idx % 4 == 0) clampedNote = clampNoteForModel(clampedNote - 12.0f, chordModel_);
        else if (idx % 4 == 3) clampedNote = clampNoteForModel(clampedNote + 12.0f, chordModel_);
    }

    chord_perf_state_.note = clampedNote;
    chord_perf_state_.strum = true;
}

static inline float soft_limit(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

static inline int16_t saturate_int16(float x) {
    int32_t i = static_cast<int32_t>(x);
    if (i > 32767) return 32767;
    if (i < -32768) return -32768;
    return static_cast<int16_t>(i);
}

void AudioStreamRings::update() {
    if (!active_ || !engine_) return;
    
    // Guard against concurrent model switching
    if (engine_->switching_) return;

    audio_block_t *blockL = allocate();
    audio_block_t *blockR = allocate();
    if (!blockL || !blockR) {
        if (blockL) release(blockL);
        if (blockR) release(blockR);
        return;
    }

    float chord_in[rings::kMaxBlockSize];
    float harp_in[rings::kMaxBlockSize] = {0};
    
    float c_out[rings::kMaxBlockSize], c_aux[rings::kMaxBlockSize];
    float h_out[rings::kMaxBlockSize], h_aux[rings::kMaxBlockSize];

    // Stereo panning weights: w_chord_L/R & w_harp_L/R
    // Separates Chord section (left hand) and Harp section (right hand) cleanly
    float width = engine_->stereo_width_;
    float w_chord_L = 0.5f + 0.5f * width;
    float w_chord_R = 0.5f - 0.5f * width;
    float w_harp_L  = 0.5f - 0.5f * width;
    float w_harp_R  = 0.5f + 0.5f * width;

    float chord_vol = engine_->chord_volume_;
    float harp_vol = engine_->harp_volume_;

    for (size_t offset = 0; offset < AUDIO_BLOCK_SAMPLES; offset += rings::kMaxBlockSize) {
        size_t n = rings::kMaxBlockSize;
        if (offset + n > AUDIO_BLOCK_SAMPLES) n = AUDIO_BLOCK_SAMPLES - offset;

        // --- Bowing excitation & Sympathetic Resonance ---
        bool harp_bow_on = (engine_->harp_held_bowing_active_ && engine_->harp_bowing_enabled_);
        bool bow_allowed = !engine_->arp_enabled_;
        
        float target_chord_gain = (engine_->bow_active_ && bow_allowed) ? (0.45f * min(1.2f, chord_vol * 1.2f)) : 0.0f;
        float target_harp_gain = (harp_bow_on && bow_allowed) ? (0.40f * min(1.2f, harp_vol * 1.2f)) : 0.0f;

        // Smooth gain transition (~360ms time constant)
        engine_->chord_bow_gain_smooth_ += 0.008f * (target_chord_gain - engine_->chord_bow_gain_smooth_);
        engine_->harp_bow_gain_smooth_ += 0.008f * (target_harp_gain - engine_->harp_bow_gain_smooth_);

        float cur_chord_g = engine_->chord_bow_gain_smooth_;
        float cur_harp_g = engine_->harp_bow_gain_smooth_;

        // Gentle Bowing Vibrato LFO (~4Hz)
        if (cur_chord_g > 0.001f || cur_harp_g > 0.001f) {
            engine_->vibrato_lfo_phase_ += 0.0015f;
            if (engine_->vibrato_lfo_phase_ > 6.2831853f) engine_->vibrato_lfo_phase_ -= 6.2831853f;
            float vib = sinf(engine_->vibrato_lfo_phase_) * 0.0025f;
            engine_->chord_perf_state_.fm = vib;
            engine_->harp_perf_state_.fm = vib;
        } else {
            engine_->chord_perf_state_.fm = 0.0f;
            engine_->harp_perf_state_.fm = 0.0f;
        }

        if (cur_chord_g > 0.0005f || cur_harp_g > 0.0005f || engine_->is_chord_held_) {
            // Sympathetic resonance bleed from chord into harp resonator
            float symp_bleed = engine_->is_chord_held_ ? (0.04f * min(1.0f, chord_vol)) : 0.0f;
            for (size_t i = 0; i < n; i++) {
                float white_noise = (static_cast<float>(stmlib::Random::GetWord()) / 2147483648.0f) - 1.0f;
                engine_->bow_filter_state_[0] += 0.25f * (white_noise - engine_->bow_filter_state_[0]);
                float exc = engine_->bow_filter_state_[0];
                chord_in[i] = exc * cur_chord_g;
                harp_in[i] = exc * cur_harp_g + (exc * symp_bleed);
            }
        } else {
            for (size_t i = 0; i < n; i++) {
                chord_in[i] = 0.0f;
                harp_in[i] = 0.0f;
            }
        }

        // --- Trigger chord notes ---
        if (engine_->chord_strum_counter_ > 0 && chord_vol > 0.01f) {
            if (engine_->chord_strum_spread_samples_ == 0 || 
                engine_->chord_strum_spread_counter_ == 0) {
                uint8_t idx = 4 - engine_->chord_strum_counter_;
                engine_->chord_perf_state_.note = engine_->chord_notes_[idx];
                engine_->chord_perf_state_.strum = true;
                engine_->chord_strum_counter_--;
                engine_->chord_strum_spread_counter_ = engine_->chord_strum_spread_samples_;
            } else {
                engine_->chord_strum_spread_counter_--;
            }
        }

        // --- Arpeggiator processing ---
        if (engine_->arp_enabled_ && engine_->arp_samples_per_step_ > 0) {
            engine_->arp_sample_counter_ += n;
            if (engine_->arp_sample_counter_ >= engine_->arp_samples_per_step_) {
                engine_->arp_sample_counter_ -= engine_->arp_samples_per_step_;
                engine_->arpTick();
            }
        }

        // Process dedicated Chord Resonator and Harp Resonator in parallel
        engine_->chord_part_.Process(engine_->chord_perf_state_, engine_->chord_patch_, chord_in, c_out, c_aux, n);
        engine_->harp_part_.Process(engine_->harp_perf_state_, engine_->harp_patch_, harp_in, h_out, h_aux, n);

        engine_->chord_perf_state_.strum = false;
        engine_->harp_perf_state_.strum = false;

        float rev_in_l[rings::kMaxBlockSize];
        float rev_in_r[rings::kMaxBlockSize];

        // Preset-controlled overdrive/distortion/wavefolding at high volume knob settings (> 60%)
        int mode = engine_->overdrive_mode_;
        float drive_mult = (mode == 3) ? 5.0f : ((mode == 2) ? 6.5f : ((mode == 1) ? 2.5f : 0.0f));
        
        float chord_drive = (mode > 0 && chord_vol > 0.60f) ? (1.0f + drive_mult * (chord_vol - 0.60f) / 0.40f) : 1.0f;
        float harp_drive  = (mode > 0 && harp_vol  > 0.60f) ? (1.0f + drive_mult * (harp_vol  - 0.60f) / 0.40f) : 1.0f;

        for (size_t i = 0; i < n; i++) {
            // Multi-Voice Stereo Chord Spreading: c_out (Voices 0 & 2) vs c_aux (Voices 1 & 3)
            float c_main = c_out[i] * chord_vol;
            float c_sub  = c_aux[i] * chord_vol;
            float chord_sig_L = c_main * (0.5f + 0.35f * width) + c_sub * (0.5f - 0.35f * width);
            float chord_sig_R = c_main * (0.5f - 0.35f * width) + c_sub * (0.5f + 0.35f * width);

            // Multi-Voice Stereo Harp Spreading: h_out (even voices) vs h_aux (odd voices)
            float h_main = h_out[i] * harp_vol;
            float h_sub  = h_aux[i] * harp_vol;
            float harp_sig_L = h_main * (0.5f - 0.35f * width) + h_sub * (0.5f + 0.35f * width);
            float harp_sig_R = h_main * (0.5f + 0.35f * width) + h_sub * (0.5f - 0.35f * width);

            // Apply overdrive / electric guitar amp distortion / wavefolding at high volume knob settings
            if (chord_drive > 1.01f) {
                float drivenL = chord_sig_L * chord_drive;
                float drivenR = chord_sig_R * chord_drive;
                if (mode == 3) {
                    chord_sig_L = sinf(sinf(drivenL * 1.57079632f) * 2.35619449f) * 0.85f;
                    chord_sig_R = sinf(sinf(drivenR * 1.57079632f) * 2.35619449f) * 0.85f;
                } else if (mode == 2) {
                    chord_sig_L = (tanhf(drivenL) + 0.15f * tanhf(drivenL * drivenL * drivenL)) / sqrtf(chord_drive);
                    chord_sig_R = (tanhf(drivenR) + 0.15f * tanhf(drivenR * drivenR * drivenR)) / sqrtf(chord_drive);
                } else {
                    chord_sig_L = tanhf(drivenL) / sqrtf(chord_drive);
                    chord_sig_R = tanhf(drivenR) / sqrtf(chord_drive);
                }
            }

            if (harp_drive > 1.01f) {
                float drivenL = harp_sig_L * harp_drive;
                float drivenR = harp_sig_R * harp_drive;
                if (mode == 3) {
                    harp_sig_L = sinf(sinf(drivenL * 1.57079632f) * 2.35619449f) * 0.85f;
                    harp_sig_R = sinf(sinf(drivenR * 1.57079632f) * 2.35619449f) * 0.85f;
                } else if (mode == 2) {
                    harp_sig_L = (tanhf(drivenL) + 0.15f * tanhf(drivenL * drivenL * drivenL)) / sqrtf(harp_drive);
                    harp_sig_R = (tanhf(drivenR) + 0.15f * tanhf(drivenR * drivenR * drivenR)) / sqrtf(harp_drive);
                } else {
                    harp_sig_L = tanhf(drivenL) / sqrtf(harp_drive);
                    harp_sig_R = tanhf(drivenR) / sqrtf(harp_drive);
                }
            }

            // Combine stereo chord and stereo harp signals with master section panning
            rev_in_l[i] = (chord_sig_L * w_chord_L) + (harp_sig_L * w_harp_L);
            rev_in_r[i] = (chord_sig_R * w_chord_R) + (harp_sig_R * w_harp_R);
        }

        // Apply Mutable Instruments Rings Dattorro Stereo Reverb
        if (engine_->reverb_mix_ > 0.001f) {
            engine_->rings_reverb_.Process(rev_in_l, rev_in_r, n);
        }

        for (size_t i = 0; i < n; i++) {
            float l = rev_in_l[i];
            float r = rev_in_r[i];

            blockL->data[offset + i] = saturate_int16(soft_limit(l) * 12000.0f);
            blockR->data[offset + i] = saturate_int16(soft_limit(r) * 12000.0f);
        }
    }

    transmit(blockL, 0);
    transmit(blockR, 1);
    release(blockL);
    release(blockR);
}
