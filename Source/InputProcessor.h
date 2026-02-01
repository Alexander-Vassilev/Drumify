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


struct MouthHit {
    int onsetSample;
    int hitLength;
    juce::AudioBuffer<float> buffer;
};

class InputProcessor {
public:
    void activate();
    void deactivate();
    void addSample(float sample);
    void processSample(float sample, float amp);
    void initBuffer();
    juce::AudioBuffer<float> hitsToBuffer();
private:
    static constexpr int numHits = 64;
    const int minOnsetSamples = 512;
    const int minOffsetSamples = 2048;
    const int samplesPerHit = 65536;
    const float ampThreshold = 0.2;
    
    std::array<MouthHit, numHits> storedHits;
    float* writePtr;
    int storedHitsIndex = 0;
    int currHitIndex = 0;
    bool isActivated = false;
    bool isNewBuffer = true; // Creating new buffer before officially activating an onset
    int currOnsetSampleCount = 0;
    int currOffsetSampleCount = 0;
    int currSample = 0;
};
