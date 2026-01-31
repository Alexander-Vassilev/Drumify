/*
  ==============================================================================

    InputProcessor.h
    Created: 31 Jan 2026 11:04:40am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#pragma once
#include <array>
#include <JuceHeader.h>

class InputProcessor {
public:
    void Activate();
    void Deactivate();
    void AddSample(float sample);
private:
    std::array<juce::AudioBuffer<float>, 64> storedHits;
    float* writePtr;
    int storedHitsIndex = 0;
    int currHitIndex = 0;
    bool isActivated = false;
    int currOnsetSampleCount = 0;
    int currOffsetSampleCount = 0;
    
    const int minOnsetSamples = 512;
    const int minOffsetSamples = 2048;
    const int samplesPerHit = 65536;
};
