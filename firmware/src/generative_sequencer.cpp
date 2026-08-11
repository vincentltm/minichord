#include "generative_sequencer.h"

GenerativeSequencer::GenerativeSequencer() 
    : _enabled(true), _density(0.75f), _path(0.0f), _mutation(0.0f), 
      _currentStep(0), _lastK(255), _rndSeed(1337) {
    static const bool defaultGrid[8] = { true, false, true, true, true, false, true, true };
    static const float defaultAccents[8] = { 1.0f, 0.6f, 0.85f, 0.7f, 0.95f, 0.6f, 0.85f, 0.7f };
    for (int i = 0; i < 8; i++) {
        _stepActive[i] = defaultGrid[i];
        _stepAccent[i] = defaultAccents[i];
        _stepRatchet[i] = false;
        _stepNoteOffset[i] = 0;
    }
    uint8_t defaultSeed[4] = {60, 64, 67, 72};
    setSeed(defaultSeed);
    updateDensityPattern();
}

uint32_t GenerativeSequencer::fastRandom(uint32_t max) {
    if (max == 0) return 0;
    _rndSeed = _rndSeed * 1103515245 + 12345;
    return (_rndSeed >> 16) % max;
}

void GenerativeSequencer::setSeed(const uint8_t notes[4]) {
    uint8_t tempBranches[7][4];
    
    // 0: Trunk (Original notes)
    for (int i = 0; i < 4; i++) {
        tempBranches[0][i] = notes[i];
    }
    
    // 1: Retrograde
    for (int i = 0; i < 4; i++) {
        tempBranches[1][i] = notes[3 - i];
    }
    
    // 2: Inversion (around root note)
    for (int i = 0; i < 4; i++) {
        int interval = notes[i] - notes[0];
        int inverted = notes[0] - interval;
        while (inverted < 0) inverted += 12;
        tempBranches[2][i] = inverted;
    }
    
    // 3: Transposition by 3rd (interval between 2nd note and root)
    int thirdInterval = notes[1] - notes[0];
    for (int i = 0; i < 4; i++) {
        int transposed = notes[i] + thirdInterval;
        while (transposed < 0) transposed += 12;
        tempBranches[3][i] = transposed;
    }
    
    // 4: Octave displacement (Deterministic seeded by chord)
    uint32_t seed = (notes[0] << 24) | (notes[1] << 16) | (notes[2] << 8) | notes[3];
    for (int i = 0; i < 4; i++) {
        seed = (seed * 1103515245 + 12345);
        int r = (seed >> 16) % 3;
        int shift = (r == 0) ? -12 : (r == 1 ? 0 : 12);
        int note = notes[i] + shift;
        while (note < 0) note += 12;
        tempBranches[4][i] = note;
    }
    
    // 5: Retrograde + Inversion
    for (int i = 0; i < 4; i++) {
        tempBranches[5][i] = tempBranches[2][3 - i];
    }
    
    // 6: Transposition + Octave displacement
    seed = (notes[1] << 24) | (notes[2] << 16) | (notes[3] << 8) | notes[0];
    for (int i = 0; i < 4; i++) {
        seed = (seed * 1103515245 + 12345);
        int r = (seed >> 16) % 3;
        int shift = (r == 0) ? -12 : (r == 1 ? 0 : 12);
        int note = tempBranches[3][i] + shift;
        while (note < 0) note += 12;
        tempBranches[6][i] = note;
    }
    
    // Update internal arrays atomically from main loop/ISR perspective
    noInterrupts();
    for (int b = 0; b < 7; b++) {
        for (int i = 0; i < 4; i++) {
            _branches[b][i] = tempBranches[b][i];
        }
    }
    for (int i = 0; i < 4; i++) {
        _seedNotes[i] = notes[i];
    }
    interrupts();
}

void GenerativeSequencer::setDensity(float density) {
    if (density < 0.0f) density = 0.0f;
    if (density > 1.0f) density = 1.0f;
    _density = density;
}

void GenerativeSequencer::setPath(float path) {
    if (path < 0.0f) path = 0.0f;
    if (path > 1.0f) path = 1.0f;
    _path = path;
}

void GenerativeSequencer::setMutation(float mutation) {
    if (mutation < 0.0f) mutation = 0.0f;
    if (mutation > 1.0f) mutation = 1.0f;
    _mutation = mutation;
}

void GenerativeSequencer::reset() {
    noInterrupts();
    _currentStep = 0;
    interrupts();
}

void GenerativeSequencer::setEnabled(bool enabled) {
    _enabled = enabled;
}

bool GenerativeSequencer::isEnabled() const {
    return _enabled;
}

void GenerativeSequencer::generateEuclidean(uint8_t k, uint8_t n, bool* pattern) {
    if (k > n) k = n;
    int bucket = 0;
    for (int i = 0; i < n; i++) {
        bucket += k;
        if (bucket >= n) {
            bucket -= n;
            pattern[i] = true;
        } else {
            pattern[i] = false;
        }
    }
}

void GenerativeSequencer::updateDensityPattern() {
    uint8_t k = (uint8_t)(_density * 8.99f); // Map 0.0-1.0 to 0-8 range
    if (k > 8) k = 8;
    
    if (k != _lastK) {
        bool newPattern[8];
        generateEuclidean(k, 8, newPattern);
        for (int i = 0; i < 8; i++) {
            _stepActive[i] = newPattern[i];
        }
        _lastK = k;
    }
}

void GenerativeSequencer::applyMutation() {
    if (_mutation <= 0.0f) return;
    
    for (int i = 0; i < 8; i++) {
        // Probability of mutation per step: mutation * 30% (so max 300 out of 1000)
        if (fastRandom(1000) < (_mutation * 300.0f)) {
            int mutationType = fastRandom(4);
            switch (mutationType) {
                case 0: { // Note shift by harmonic interval
                    int intervals[] = {-12, -7, -5, 5, 7, 12};
                    _stepNoteOffset[i] += intervals[fastRandom(6)];
                    // Bound the offset
                    if (_stepNoteOffset[i] < -24) _stepNoteOffset[i] = -24;
                    if (_stepNoteOffset[i] > 24) _stepNoteOffset[i] = 24;
                    break;
                }
                case 1: { // Toggle ratchet
                    _stepRatchet[i] = !_stepRatchet[i];
                    break;
                }
                case 2: { // Accent variation
                    float change = ((float)fastRandom(41) - 20.0f) / 100.0f; // -0.2 to +0.2
                    _stepAccent[i] += change;
                    if (_stepAccent[i] < 0.1f) _stepAccent[i] = 0.1f;
                    if (_stepAccent[i] > 1.0f) _stepAccent[i] = 1.0f;
                    break;
                }
                case 3: { // Toggle active/inactive
                    _stepActive[i] = !_stepActive[i];
                    break;
                }
            }
        }
    }
}

SequencerEvent GenerativeSequencer::tick() {
    SequencerEvent ev;
    ev.active = false;
    ev.noteValue = 0;
    ev.voiceIndex = 0;
    ev.accent = 0.0f;
    ev.ratchet = false;
    
    if (!_enabled) return ev;
    
    if (_currentStep == 0) {
        updateDensityPattern();
        applyMutation();
    }
    
    static const uint8_t arpVoiceMap[8] = { 0, 1, 2, 3, 2, 1, 0, 3 };

    ev.active = _stepActive[_currentStep];
    ev.accent = _stepAccent[_currentStep];
    ev.ratchet = _stepRatchet[_currentStep];
    ev.voiceIndex = arpVoiceMap[_currentStep];
    
    if (ev.active) {
        float p = _path;
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        
        float scaledPath = p * 6.0f;
        int branchIndex = (int)scaledPath;
        if (branchIndex > 5) branchIndex = 5;
        
        float fraction = scaledPath - (float)branchIndex;
        int selectedBranch = branchIndex;
        
        if (fraction > 0.01f && (fastRandom(1000) < (uint32_t)(fraction * 1000.0f))) {
            selectedBranch = branchIndex + 1;
        }
        
        uint8_t noteIdx = arpVoiceMap[_currentStep];
        
        noInterrupts();
        int baseNote = _branches[selectedBranch][noteIdx];
        interrupts();
        
        int finalNote = baseNote + _stepNoteOffset[_currentStep];
        
        // Wrap gracefully within MIDI bounds rather than hard clamping
        while (finalNote < 0) finalNote += 12;
        while (finalNote > 127) finalNote -= 12;
        
        ev.noteValue = (uint8_t)finalNote;
    }
    
    _currentStep = (_currentStep + 1) % 8;
    return ev;
}
