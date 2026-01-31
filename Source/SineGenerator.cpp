/*
  ==============================================================================

    SineGenerator.cpp
    Created: 29 Jan 2026 10:38:03pm
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include <JuceHeader.h>
#include "SineGenerator.h"
#include <cmath>

void SineGenerator::prepare(const double sampleRate, const int numChannels) {
    currentSampleRate = static_cast<float>(sampleRate);
};

void SineGenerator::process(float* output, int numSamples, const float* amplitudes) {
    float phaseInc = doublePi * frequency / currentSampleRate;
    
    for (int i = 0; i < numSamples; i++) {
        output[i] = abs(amplitudes[i]) * std::sinf(phase);
        phase += phaseInc;
    }
};

float SineGenerator::processSample(float amp) {
    float phaseInc = doublePi * frequency / currentSampleRate;
    float sample = amp * std::sinf(phase);
    phase += phaseInc;
    if (phase >= juce::MathConstants<float>::twoPi) {
        phase -= juce::MathConstants<float>::twoPi;
    }

    return sample;
};
