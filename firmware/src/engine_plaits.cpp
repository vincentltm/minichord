#include "engine_plaits.h"
#include <cstring>
#include <algorithm>

void AudioStreamPlaits::update() {
    audio_block_t *blockL = allocate();
    audio_block_t *blockR = allocate();
    
    if (!blockL || !blockR) {
        if (blockL) release(blockL);
        if (blockR) release(blockR);
        return;
    }
    
    if (active_ && engine_) {
        engine_->renderAudioBlock(blockL, blockR);
    } else {
        memset(blockL->data, 0, sizeof(blockL->data));
        memset(blockR->data, 0, sizeof(blockR->data));
    }
    
    transmit(blockL, 0);
    transmit(blockR, 1);
    release(blockL);
    release(blockR);
}

EnginePlaits::EnginePlaits() 
    : audioStream_(this),
      currentModel_(0),
      harmonics_(0.5f),
      timbre_(0.5f),
      morph_(0.5f),
      lpgDecay_(0.5f),
      lpgColour_(0.5f),
      volume_(0.8f),
      polyphony_(6),
      currentRootNote_(60),
      chordActive_(false) {
    
    memset(&patch_, 0, sizeof(patch_));
    patch_.note = 60.0f;
    patch_.harmonics = harmonics_;
    patch_.timbre = timbre_;
    patch_.morph = morph_;
    patch_.engine = currentModel_;
    patch_.decay = lpgDecay_;
    patch_.lpg_colour = lpgColour_;

    for (size_t i = 0; i < MAX_PLAITS_VOICES; ++i) {
        voiceStates_[i].active = false;
        voiceStates_[i].note = 60;
        voiceStates_[i].level = 0.0f;
        voiceStates_[i].triggerPulse = 0.0f;
        voiceStates_[i].age = 0;
        
        memset(&modulations_[i], 0, sizeof(modulations_[i]));
    }
    
    memset(currentChord_, 0, sizeof(currentChord_));
}

DMAMEM static uint8_t plaitsBufferPool[4 * 16384];

void EnginePlaits::init() {
    for (size_t i = 0; i < MAX_PLAITS_VOICES; ++i) {
        stmlib::BufferAllocator allocator(&plaitsBufferPool[i * 16384], 16384);
        voices_[i].Init(&allocator);
    }
    audioStream_.setActive(false);
}

void EnginePlaits::activate() {
    audioStream_.setActive(true);
}

void EnginePlaits::deactivate() {
    audioStream_.setActive(false);
    for (size_t i = 0; i < MAX_PLAITS_VOICES; ++i) {
        voiceStates_[i].active = false;
    }
}

struct PlaitsModelDefaults {
    float harmonics;
    float timbre;
    float morph;
    float decay;
    float lpgColour;
};

static const PlaitsModelDefaults kPlaitsDefaults[] = {
    { 0.50f, 0.65f, 0.40f, 0.60f, 0.50f }, // 0: VA
    { 0.35f, 0.55f, 0.45f, 0.55f, 0.50f }, // 1: Waveshaper
    { 0.40f, 0.45f, 0.60f, 0.70f, 0.40f }, // 2: 2-Op FM
    { 0.50f, 0.70f, 0.50f, 0.65f, 0.50f }, // 3: Grain / Formant
    { 0.60f, 0.50f, 0.50f, 0.75f, 0.60f }, // 4: Additive Harmonic
    { 0.45f, 0.60f, 0.70f, 0.80f, 0.50f }, // 5: Wavetable
    { 0.50f, 0.50f, 0.60f, 0.70f, 0.50f }, // 6: Chord Engine
    { 0.50f, 0.60f, 0.50f, 0.65f, 0.50f }, // 7: Speech
    { 0.40f, 0.65f, 0.55f, 0.75f, 0.50f }, // 8: Swarm
    { 0.50f, 0.50f, 0.50f, 0.40f, 0.50f }, // 9: Noise
    { 0.40f, 0.60f, 0.50f, 0.50f, 0.50f }, // 10: Particle
    { 0.50f, 0.50f, 0.50f, 0.60f, 0.50f }, // 11: String
    { 0.45f, 0.70f, 0.60f, 0.65f, 0.50f }, // 12: Modal
    { 0.30f, 0.40f, 0.50f, 0.45f, 0.50f }, // 13: Kick
    { 0.50f, 0.50f, 0.50f, 0.35f, 0.50f }, // 14: Snare
    { 0.60f, 0.70f, 0.50f, 0.25f, 0.60f }, // 15: HiHat
    { 0.50f, 0.60f, 0.50f, 0.60f, 0.50f }, // 16: VA VCF
    { 0.50f, 0.50f, 0.50f, 0.60f, 0.50f }, // 17: Phase Distortion
    { 0.40f, 0.50f, 0.50f, 0.70f, 0.50f }, // 18: Six-Op FM
    { 0.50f, 0.60f, 0.50f, 0.70f, 0.50f }, // 19: Wave Terrain
    { 0.50f, 0.50f, 0.50f, 0.75f, 0.50f }, // 20: String Machine
    { 0.50f, 0.50f, 0.50f, 0.50f, 0.50f }  // 21: Chiptune
};

void EnginePlaits::setSubModel(int32_t subModel) {
    if (subModel >= 0 && subModel < PLAITS_MODEL_COUNT) {
        currentModel_ = subModel;
        patch_.engine = currentModel_;

        // Load tuned preset defaults for the selected model
        if (subModel < static_cast<int32_t>(sizeof(kPlaitsDefaults) / sizeof(kPlaitsDefaults[0]))) {
            harmonics_ = kPlaitsDefaults[subModel].harmonics;
            timbre_ = kPlaitsDefaults[subModel].timbre;
            morph_ = kPlaitsDefaults[subModel].morph;
            lpgDecay_ = kPlaitsDefaults[subModel].decay;
            lpgColour_ = kPlaitsDefaults[subModel].lpgColour;

            patch_.harmonics = harmonics_;
            patch_.timbre = timbre_;
            patch_.morph = morph_;
            patch_.decay = lpgDecay_;
            patch_.lpg_colour = lpgColour_;
        }
    }
}

void EnginePlaits::triggerVoice(uint8_t note, float velocity) {
    // Find free voice or steal oldest
    int targetIdx = -1;
    uint32_t oldestAge = 0;

    for (size_t i = 0; i < polyphony_; ++i) {
        if (!voiceStates_[i].active) {
            targetIdx = i;
            break;
        }
        if (voiceStates_[i].age >= oldestAge) {
            oldestAge = voiceStates_[i].age;
            targetIdx = i;
        }
    }

    if (targetIdx >= 0) {
        voiceStates_[targetIdx].active = true;
        voiceStates_[targetIdx].note = note;
        voiceStates_[targetIdx].level = velocity;
        voiceStates_[targetIdx].triggerPulse = 1.0f;
        voiceStates_[targetIdx].age = 0;
    }
}

void EnginePlaits::releaseVoice(uint8_t note) {
    // In Plaits, releasing a touch pad allows the internal Low-Pass Gate (LPG)
    // to complete its natural decay envelope (controlled by Knob 3 / patch_.decay).
    // We do NOT zero out voiceStates_[i].level so the LPG tail decays naturally.
}

void EnginePlaits::onChordChange(uint8_t rootNote, const uint8_t chord[7], bool sharp, bool maj, bool min, bool sev) {
    currentRootNote_ = rootNote;
    memcpy(currentChord_, chord, sizeof(currentChord_));
    chordActive_ = true;
    
    // Auto-strum chord on chord button change
    for (int i = 0; i < 3; ++i) {
        if (currentChord_[i] > 0) {
            triggerVoice(currentChord_[i], 0.7f);
        }
    }
}

void EnginePlaits::onChordRelease() {
    chordActive_ = false;
}

void EnginePlaits::onHarpTouch(uint8_t padIndex, bool pressed, uint8_t noteValue, float pressure) {
    if (pressed) {
        triggerVoice(noteValue, pressure);
    } else {
        releaseVoice(noteValue);
    }
}

void EnginePlaits::onPotChange(uint8_t potIndex, float normalizedValue, bool shifted, bool holdPressed) {
    if (holdPressed) return;

    if (potIndex == 0) {
        if (!shifted) {
            volume_ = normalizedValue; // Pot 0 Unshifted = Main / Engine Volume
        } else {
            harmonics_ = normalizedValue; // Pot 0 Shifted = HARMONICS
            patch_.harmonics = harmonics_;
        }
    } else if (potIndex == 1) {
        if (!shifted) {
            volume_ = normalizedValue; // Pot 1 Unshifted = Secondary Level
        } else {
            timbre_ = normalizedValue; // Pot 1 Shifted = TIMBRE
            patch_.timbre = timbre_;
        }
    } else if (potIndex == 2) {
        if (!shifted) {
            // Pot 2 Unshifted = LPG Decay (Decay time tail duration, 0.10f to 1.0f)
            lpgDecay_ = 0.10f + 0.90f * normalizedValue;
            patch_.decay = lpgDecay_;
        } else {
            morph_ = normalizedValue; // Pot 2 Shifted = MORPH
            patch_.morph = morph_;
        }
    }
}

void EnginePlaits::onSequencerNote(uint8_t voiceIndex, uint8_t noteValue, float accent) {
    triggerVoice(noteValue, accent);
}

void EnginePlaits::onHoldToggle(bool continuous) {
    // Hold toggle handler
}

void EnginePlaits::renderAudioBlock(audio_block_t* blockL, audio_block_t* blockR) {
    memset(blockL->data, 0, sizeof(blockL->data));
    memset(blockR->data, 0, sizeof(blockR->data));

    plaits::Voice::Frame frames[AUDIO_BLOCK_SAMPLES];

    for (size_t v = 0; v < polyphony_; ++v) {
        if (!voiceStates_[v].active) continue;

        voiceStates_[v].age++;

        plaits::Patch vPatch = patch_;
        vPatch.note = static_cast<float>(voiceStates_[v].note);
        vPatch.engine = currentModel_;

        plaits::Modulations vMod = modulations_[v];
        vMod.trigger = voiceStates_[v].triggerPulse;
        vMod.level = voiceStates_[v].level;
        vMod.trigger_patched = true;
        vMod.level_patched = true;

        // Render Plaits voice frame block
        voices_[v].Render(vPatch, vMod, frames, AUDIO_BLOCK_SAMPLES);

        // Reset trigger pulse after initial frame render so trigger acts as an impulse strike
        voiceStates_[v].triggerPulse = 0.0f;

        // Auto-deactivate voice after extended idle age to keep voice pool clean
        if (voiceStates_[v].age > 1500) {
            voiceStates_[v].active = false;
        }

        // Accumulate both main (out) and sub/aux (aux) audio signals into block buffer
        for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            float mixedFrame = (static_cast<float>(frames[i].out) + static_cast<float>(frames[i].aux)) * 0.5f;
            int32_t outSample = static_cast<int32_t>(mixedFrame * volume_);
            blockL->data[i] = static_cast<int16_t>(stmlib::Clip16(blockL->data[i] + outSample));
            blockR->data[i] = static_cast<int16_t>(stmlib::Clip16(blockR->data[i] + outSample));
        }
    }
}

void EnginePlaits::sendMidi() {
    // MIDI output handling
}

void EnginePlaits::applyParameter(uint16_t address, int16_t value) {
    switch (address) {
        case 260: // Plaits Engine Model (0-21)
            setSubModel(value);
            break;
        case 261: // Harmonics (0-1000)
            harmonics_ = constrain(value / 1000.0f, 0.0f, 1.0f);
            patch_.harmonics = harmonics_;
            break;
        case 262: // Timbre (0-1000)
            timbre_ = constrain(value / 1000.0f, 0.0f, 1.0f);
            patch_.timbre = timbre_;
            break;
        case 263: // Morph (0-1000)
            morph_ = constrain(value / 1000.0f, 0.0f, 1.0f);
            patch_.morph = morph_;
            break;
        case 264: // LPG Decay (0-1000)
            lpgDecay_ = constrain(value / 1000.0f, 0.0f, 1.0f);
            patch_.decay = lpgDecay_;
            break;
        case 265: // LPG Colour (0-1000)
            lpgColour_ = constrain(value / 1000.0f, 0.0f, 1.0f);
            patch_.lpg_colour = lpgColour_;
            break;
        case 266: // Polyphony (1-6)
            polyphony_ = constrain(value, 1, MAX_PLAITS_VOICES);
            break;
    }
}

void EnginePlaits::getParameterDefaults(int16_t* dest, size_t size) {
    if (size > 266) {
        dest[260] = currentModel_;
        dest[261] = static_cast<int16_t>(harmonics_ * 1000.0f);
        dest[262] = static_cast<int16_t>(timbre_ * 1000.0f);
        dest[263] = static_cast<int16_t>(morph_ * 1000.0f);
        dest[264] = static_cast<int16_t>(lpgDecay_ * 1000.0f);
        dest[265] = static_cast<int16_t>(lpgColour_ * 1000.0f);
        dest[266] = polyphony_;
    }
}

const char* EnginePlaits::presetFilename(uint8_t bankIndex) {
    static char fn[32];
    snprintf(fn, sizeof(fn), "plaits_bank_%d.dat", bankIndex);
    return fn;
}

float EnginePlaits::ledHue() const {
    static const float plaits_model_hues[] = {
        30.0f,  // 0: VA (Amber / Orange)
        0.0f,   // 1: Waveshape (Crimson Red)
        180.0f, // 2: 2-Op FM (Electric Cyan)
        280.0f, // 3: Grain (Neon Purple)
        55.0f,  // 4: Additive (Golden Yellow)
        210.0f, // 5: Wavetable (Deep Sky Blue)
        120.0f, // 6: Chord (Bright Green)
        320.0f, // 7: Speech (Hot Magenta / Pink)
        15.0f,  // 8: Swarm (Fiery Red-Orange)
        195.0f, // 9: Noise (Ice Blue)
        160.0f, // 10: Particle (Emerald Teal)
        40.0f,  // 11: String (Warm Copper)
        260.0f, // 12: Modal (Rich Indigo)
        350.0f, // 13: Kick (Deep Ruby Red)
        90.0f,  // 14: Snare (Lime Green)
        175.0f, // 15: HiHat (Silver Cyan)
        35.0f,  // 16: VA VCF
        50.0f,  // 17: Phase Distortion
        185.0f, // 18: Six-Op FM
        205.0f, // 19: Wave Terrain
        270.0f, // 20: String Machine
        100.0f  // 21: Chiptune
    };
    if (currentModel_ >= 0 && currentModel_ < static_cast<int>(sizeof(plaits_model_hues) / sizeof(plaits_model_hues[0]))) {
        return plaits_model_hues[currentModel_];
    }
    return 180.0f;
}
