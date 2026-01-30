/*
  ==============================================================================

    SineGenerator.h
    Created: 29 Jan 2026 10:38:17pm
    Author:  Alexander Vassilev

  ==============================================================================
*/

#pragma once

#include <numbers>

class SineGenerator {
public:
    void process(float* output, int numSamples, const float* amplitude);
    float processSample(float amp);
    void prepare(const double sampleRate, const int numChannels);
private:
    float doublePi = 2.0f * std::numbers::pi_v<float>;
    float frequency = 440;
    float currentSampleRate = 0;
    float phase = 0;
};

