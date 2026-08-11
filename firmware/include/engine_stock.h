#pragma once
#include "engine_interface.h"
#include <functional>

// Engine 0: Stock Minichord subtractive synthesizer.
// When active, the existing main.cpp audio processing runs unmodified.
// This wrapper handles activation/deactivation of the Teensy Audio Library
// graph when switching between engines.
class EngineStock : public MinichordEngine {
public:
    EngineStock() : active_(true) {}
    
    void init() override {
        // Stock engine is initialized by main.cpp setup() directly
    }
    
    std::function<void()> on_activate_cb;
    std::function<void()> on_deactivate_cb;

    void setCallbacks(const std::function<void()>& onAct, const std::function<void()>& onDeact) {
        on_activate_cb = onAct;
        on_deactivate_cb = onDeact;
    }

    void activate() override {
        active_ = true;
        if (on_activate_cb) on_activate_cb();
    }
    
    void deactivate() override {
        active_ = false;
        if (on_deactivate_cb) on_deactivate_cb();
    }
    
    // When stock engine is active, main.cpp handles all input directly.
    // These methods are no-ops for engine 0.
    void onChordChange(uint8_t, const uint8_t[7], bool, bool, bool, bool) override {}
    void onChordRelease() override {}
    void onHarpTouch(uint8_t, bool, uint8_t, float = 1.0f) override {}
    void onPotChange(uint8_t, float, bool, bool) override {}
    void onHoldToggle(bool) override {}
    
    // Stock engine audio is handled by the Teensy Audio Library graph
    void processAudio(int16_t*, int16_t*, size_t) override {}
    
    void sendMidi() override {}
    
    void applyParameter(uint16_t, int16_t) override {
        // Stock engine parameters are applied via the existing
        // apply_audio_parameter() function in sysex_handler.h
    }
    
    void getParameterDefaults(int16_t* dest, size_t size) override {
        // Stock defaults are defined in default_bank_sysex_parameters[]
    }
    
    const char* presetFilename(uint8_t bankIndex) override {
        static const char* names[] = {
            "a.txt", "b.txt", "c.txt", "d.txt", "e.txt", "f.txt",
            "g.txt", "h.txt", "i.txt", "j.txt", "k.txt", "l.txt"
        };
        return names[bankIndex % 12];
    }
    
    const char* name() const override { return "Stock"; }
    float ledHue() const override { return -1.0f; }  // Use preset-defined hue
    
    bool isActive() const { return active_; }
    
private:
    bool active_;
};
