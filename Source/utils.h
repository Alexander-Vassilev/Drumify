/*
  ==============================================================================

    utils.h
    Created: 24 May 2026 5:24:47pm
    Author:  Alexander Vassilev

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "FFTProcessor.h"

static constexpr int numFilters = 26;

float hzToMel(float hz);
float melToHz(float mel);
std::array<float, numFilters> applyMelFilterbank(const std::array<float, FFTProcessor::numBins>& spectrum, double sampleRate);
float bandIndexToHz(int bandIndex, double sampleRate);
