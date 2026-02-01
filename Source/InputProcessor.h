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
#include "hitClassifier.h"
#include "classifiedHit.h"
#include <vector>

struct MouthHit {
    int onsetSample;
    int hitLength;
    juce::AudioBuffer<float> buffer;
    
    HitType type = HitType::Unknown;
};


class InputProcessor {
public:
    void activate();
    void deactivate();
    void addSample(float sample);
    void processSample(float sample, float amp);
    void initBuffer();
    void reset();
    
    juce::AudioBuffer<float> hitsToBuffer();
    
    int storedHitsIndex = 0;
    void classifyStoredHits(double sampleRate);
    static constexpr int numHits = 64;
    std::array<MouthHit, numHits> storedHits;
    // Results live here:
    std::vector<ClassifiedHit> classifiedHits;
private:
    /*
    static constexpr int numHits = 64;
    const int minOnsetSamples = 512;
    const int minOffsetSamples = 2048;
    const int samplesPerHit = 65536;
    const float ampThreshold = 0.08;
    
    std::array<MouthHit, numHits> storedHits;
    float* writePtr;
    int currHitIndex = 0;
    bool isActivated = false;
    bool isNewBuffer = true; // Creating new buffer before officially activating an onset
    int currOnsetSampleCount = 0;
    int currOffsetSampleCount = 0;
    int currSample = 0;
     */


    // --- Detection parameters ---
    static constexpr float onsetThreshold  = 0.05f;
    static constexpr float offsetThreshold = 0.04f;
    static constexpr float noveltyThreshold = 0.015f;

    static constexpr int minOnsetSamples  = 128;
    static constexpr int minOffsetSamples = 1024;

    // --- Buffering ---
    static constexpr int samplesPerHit   = 65536;
    static constexpr int preRollSamples  = 256;


    float preRoll[preRollSamples] = {};
    int preRollIndex = 0;

    float* writePtr = nullptr;

    //int storedHitsIndex = 0;
    int currHitIndex = 0;

    int currSample = 0;
    int onsetCounter = 0;
    int offsetCounter = 0;

    float previousAmp = 0.0f;

    bool isActivated = false;
};
