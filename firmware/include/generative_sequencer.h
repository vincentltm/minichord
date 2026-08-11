#pragma once
#include <Arduino.h>

struct SequencerEvent {
    bool active;        // true if this step fires a note, false if rest
    uint8_t noteValue;  // the note to play (semitone offset from C, same scale as chord notes)
    uint8_t voiceIndex; // which chord voice (0-3) this note targets
    float accent;       // 0.0-1.0 velocity/accent for this step
    bool ratchet;       // if true, this step should double-trigger (ratchet)
};

class GenerativeSequencer {
public:
    GenerativeSequencer();
    
    // Set the chord seed — rebuilds the fractal tree
    // notes[] contains 4 notes as semitone offsets (e.g., 0, 4, 7, 12 for Cmaj)
    void setSeed(const uint8_t notes[4]);
    
    // Real-time parameter control (all 0.0 to 1.0)
    void setDensity(float density);   // Knob 1: how many steps fire per cycle
    void setPath(float path);         // Knob 2: navigate fractal tree branches
    void setMutation(float mutation); // Knob 3: how much pattern evolves over time
    
    // Advance one step, returns the event for this step
    SequencerEvent tick();
    
    // Reset to step 0
    void reset();
    
    // Enable/disable
    void setEnabled(bool enabled);
    bool isEnabled() const;
    
private:
    volatile bool _enabled;
    volatile float _density;
    volatile float _path;
    volatile float _mutation;
    
    volatile uint8_t _branches[7][4];
    volatile uint8_t _seedNotes[4];
    
    uint8_t _currentStep;
    uint8_t _lastK; // To track density changes
    
    // Generated pattern state
    bool _stepActive[8];
    float _stepAccent[8];
    bool _stepRatchet[8];
    int8_t _stepNoteOffset[8];
    
    // Lightweight pseudo-random for ISR safety
    uint32_t _rndSeed;
    uint32_t fastRandom(uint32_t max);
    
    void generateEuclidean(uint8_t k, uint8_t n, bool* pattern);
    void applyMutation();
    void updateDensityPattern();
};
