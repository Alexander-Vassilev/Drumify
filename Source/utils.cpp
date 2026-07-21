/*
  ==============================================================================

    utils.cpp
    Created: 24 May 2026 5:24:59pm
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "utils.h"

float hzToMel(float hz) {return 2595.0f * std::log10(1.0f + (hz / 700.0f));}
float melToHz(float mel) {return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);}

std::array<float, numFilters> applyMelFilterbank(const std::array<float, FFTProcessor::numBins>& spectrum, double sampleRate) {
    const float nyquist = sampleRate / 2.0f;
    const float melMin = hzToMel(50.0f);
    const float melMax = hzToMel(nyquist);
    //std::cout << "applying mel filter\n";
    //for (int i = 0; i < numFilters + 2; i++) {
    //    std::cout << std::fixed << std::setprecision(1) << std::setw(6) << i << " ";
    //}
    
    //std::cout << std::endl;
    
    std::array<float, numFilters + 2> centerHz;
    for (int i = 0; i < numFilters + 2; i++) {
        float mel = melMin + (melMax - melMin) * i / (numFilters + 1);
        centerHz[i] = melToHz(mel);
        //std::cout << std::fixed << std::setprecision(0) << std::setw(6) << centerHz[i] << " ";
    }
    
    //std::cout << std::endl;
    
    auto hzToBin = [&](float hz) {
       return (int)(hz / nyquist * (FFTProcessor::numBins - 1));
    };

    std::array<float, numFilters> output{};
    
    for (int m = 1; m <= numFilters; m++) {
        int left   = hzToBin(centerHz[m - 1]);
        int center = hzToBin(centerHz[m]);
        int right  = hzToBin(centerHz[m + 1]);

        for (int k = left; k < center; k++)
            output[m-1] += spectrum[k] * (float)(k - left) / (center - left);
        for (int k = center; k <= right; k++)
            output[m-1] += spectrum[k] * (float)(right - k) / (right - center);
    }
    
    return output;
};

float bandIndexToHz(int bandIndex, double sampleRate) {
    const float nyquist = (float)sampleRate / 2.0f;
    const float melMin = hzToMel(80.0f);
    const float melMax = hzToMel(nyquist);

    // +1 because band 0 corresponds to centerHz[1] (the first real filter center,
    // since centerHz[0] is the left edge of the first triangle)
    float mel = melMin + (melMax - melMin) * (bandIndex + 1) / (numFilters + 1);
    return melToHz(mel);
};
