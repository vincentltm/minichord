#pragma once

#include <Arduino.h>
#include <Audio.h>
#include <AudioStream.h>
#include "engine_interface.h"
#include "teensy_compat.h"
#include "plaits/dsp/voice.h"
#include "plaits/dsp/envelope.h"
#include "stmlib/utils/buffer_allocator.h"
#include "master_effects.h"

extern MasterEffects masterEffects;

enum PlaitsModel {
    PLAITS_MODEL_VIRTUAL_ANALOG = 0,
    PLAITS_MODEL_WAVESHAPING,
    PLAITS_MODEL_FM,
    PLAITS_MODEL_GRAIN,
    PLAITS_MODEL_ADDITIVE,
    PLAITS_MODEL_WAVETABLE,
    PLAITS_MODEL_CHORD,
    PLAITS_MODEL_SPEECH,
    PLAITS_MODEL_SWARM,
    PLAITS_MODEL_NOISE,
    PLAITS_MODEL_PARTICLE,
    PLAITS_MODEL_STRING,
    PLAITS_MODEL_MODAL,
    PLAITS_MODEL_BASS_DRUM,
    PLAITS_MODEL_SNARE_DRUM,
    PLAITS_MODEL_HI_HAT,
    PLAITS_MODEL_VA_VCF,
    PLAITS_MODEL_PHASE_DISTORTION,
    PLAITS_MODEL_SIX_OP_FM,
    PLAITS_MODEL_WAVE_TERRAIN,
    PLAITS_MODEL_STRING_MACHINE,
    PLAITS_MODEL_CHIPTUNE,
    PLAITS_MODEL_COUNT
};

class EnginePlaits;

class AudioStreamPlaits : public AudioStream {
public:
    AudioStreamPlaits(EnginePlaits* engine) : AudioStream(0, nullptr), engine_(engine), active_(false) {}
    virtual void update() override;
    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }

private:
    EnginePlaits* engine_;
    volatile bool active_;
};

class EnginePlaits : public MinichordEngine {
public:
    EnginePlaits();
    virtual ~EnginePlaits() = default;

    void init() override;
    void activate() override;
    void deactivate() override;
    void setSubModel(int32_t subModel) override;
    AudioStreamPlaits* getStream() { return &audioStream_; }

    void onChordChange(uint8_t rootNote, const uint8_t chord[7], bool sharp, bool maj, bool min, bool sev) override;
    void onChordRelease() override;
    void onHarpTouch(uint8_t padIndex, bool pressed, uint8_t noteValue, float pressure = 1.0f) override;
    void onPotChange(uint8_t potIndex, float normalizedValue, bool shifted, bool holdPressed) override;
    void onSequencerNote(uint8_t voiceIndex, uint8_t noteValue, float accent) override;
    void onHoldToggle(bool continuous) override;

    void processAudio(int16_t* outputL, int16_t* outputR, size_t blockSize) override {}

    void renderAudioBlock(audio_block_t* blockL, audio_block_t* blockR);

    void sendMidi() override;
    void applyParameter(uint16_t address, int16_t value) override;
    void getParameterDefaults(int16_t* dest, size_t size) override;

    const char* presetFilename(uint8_t bankIndex) override;
    const char* name() const override { return "Plaits Engine"; }
    float ledHue() const override { return 180.0f; } // Cyan/Turquoise LED color

private:
    static const size_t MAX_PLAITS_VOICES = 4;
    
    struct VoiceState {
        bool active;
        uint8_t note;
        float level;
        float triggerPulse;
        uint32_t age;
    };

    plaits::Voice voices_[MAX_PLAITS_VOICES];
    VoiceState voiceStates_[MAX_PLAITS_VOICES];
    
    AudioStreamPlaits audioStream_;

    plaits::Patch patch_;
    plaits::Modulations modulations_[MAX_PLAITS_VOICES];
    
    int currentModel_;
    float harmonics_;
    float timbre_;
    float morph_;
    float lpgDecay_;
    float lpgColour_;
    float volume_;
    uint8_t polyphony_;
    
    uint8_t currentChord_[7];
    uint8_t currentRootNote_;
    bool chordActive_;

    void triggerVoice(uint8_t note, float velocity);
    void releaseVoice(uint8_t note);
};
