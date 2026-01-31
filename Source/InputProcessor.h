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
    void process(float* output, int numSamples, const float* amplitude);
    float processSample(float amp);
    void prepare(const double sampleRate, const int numChannels);
private:
    std::array<juce::AudioBuffer<float>&, 64> storedHits;
    
    float currentSampleRate = 0;
    float phase = 0;
};
