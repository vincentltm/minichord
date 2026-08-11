#pragma once
#include <cstdint>
#include <cstddef>

// Abstract interface for Minichord sound engines.
// Each engine implements audio rendering, hardware input handling,
// MIDI output, and sysex parameter management.
class MinichordEngine {
public:
    virtual ~MinichordEngine() {}
    
    // Lifecycle
    virtual void init() = 0;
    virtual void activate() = 0;    // Called when this engine becomes the active mode
    virtual void deactivate() = 0;  // Called when switching away from this engine
    virtual void setSubModel(int32_t subModel) {}

    // Hardware input events (called from main loop)
    virtual void onChordChange(uint8_t rootNote, const uint8_t chord[7], bool sharp, bool maj, bool min, bool sev) = 0;
    virtual void onChordRelease() = 0;
    virtual void onHarpTouch(uint8_t padIndex, bool pressed, uint8_t noteValue, float pressure = 1.0f) = 0;
    virtual void onPotChange(uint8_t potIndex, float normalizedValue, bool shifted, bool holdPressed) = 0;
    virtual void onHoldToggle(bool continuous) = 0;
    
    // Generative sequencer note trigger (called from main loop when sequencer fires)
    virtual void onSequencerNote(uint8_t voiceIndex, uint8_t noteValue, float accent) {}


    // Audio rendering (called from Teensy Audio ISR context)
    virtual void processAudio(int16_t* outputL, int16_t* outputR, size_t blockSize) = 0;

    // MIDI output (called from main loop)
    virtual void sendMidi() = 0;

    // Sysex parameter system
    virtual void applyParameter(uint16_t address, int16_t value) = 0;
    virtual void getParameterDefaults(int16_t* dest, size_t size) = 0;
    
    // Preset persistence (each engine has its own file namespace)
    virtual const char* presetFilename(uint8_t bankIndex) = 0;

    // Identity
    virtual const char* name() const = 0;
    virtual float ledHue() const = 0;  // HSV hue (0-360) for RGB LED
};
