#pragma once
#include "engine_interface.h"
#include <Arduino.h>

// Represents a single mode entry in the flat mode list
struct ModeEntry {
    const char* name;
    MinichordEngine* engine;
    int32_t subModel;  // -1 for stock, or rings::ResonatorModel value
};

// Manages the flat list of modes and handles switching between them.
// Modes cycle: Stock -> Modal -> Sympathetic -> Inharmonic -> FM
class EngineManager {
public:
    static const uint8_t MAX_MODES = 8;
    
    EngineManager() : modeCount_(0), currentMode_(0) {}
    
    void registerMode(const char* name, MinichordEngine* engine, int32_t subModel = -1) {
        if (modeCount_ < MAX_MODES) {
            modes_[modeCount_] = {name, engine, subModel};
            modeCount_++;
        }
    }
    
    void nextMode() {
        if (modeCount_ == 0) return;
        modes_[currentMode_].engine->deactivate();
        currentMode_ = (currentMode_ + 1) % modeCount_;
        modes_[currentMode_].engine->setSubModel(modes_[currentMode_].subModel);
        modes_[currentMode_].engine->activate();
        Serial.print("> Mode: ");
        Serial.println(modes_[currentMode_].name);
    }
    
    void prevMode() {
        if (modeCount_ == 0) return;
        modes_[currentMode_].engine->deactivate();
        currentMode_ = (currentMode_ == 0) ? modeCount_ - 1 : currentMode_ - 1;
        modes_[currentMode_].engine->setSubModel(modes_[currentMode_].subModel);
        modes_[currentMode_].engine->activate();
        Serial.print("> Mode: ");
        Serial.println(modes_[currentMode_].name);
    }
    
    MinichordEngine* activeEngine() {
        return modes_[currentMode_].engine;
    }
    
    int32_t activeSubModel() const {
        return modes_[currentMode_].subModel;
    }
    
    uint8_t currentModeIndex() const {
        return currentMode_;
    }
    
    const char* currentModeName() const {
        return modes_[currentMode_].name;
    }
    
    uint8_t modeCount() const {
        return modeCount_;
    }

    void setModeIndex(uint8_t idx) {
        if (idx >= modeCount_) return;
        if (idx == currentMode_) return;
        modes_[currentMode_].engine->deactivate();
        currentMode_ = idx;
        modes_[currentMode_].engine->setSubModel(modes_[currentMode_].subModel);
        modes_[currentMode_].engine->activate();
    }

private:
    ModeEntry modes_[MAX_MODES];
    uint8_t modeCount_;
    uint8_t currentMode_;
};
