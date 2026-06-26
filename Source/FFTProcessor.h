/*
  ==============================================================================

    FFTProcessor.h
    Created: 24 May 2026 11:27:05am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>


class FFTProcessor {
public:
    FFTProcessor();
    
    inline static constexpr int fftOrder = 10;
    inline static constexpr int fftSize = 1 << fftOrder;
    inline static constexpr int numBins = fftSize / 2 + 1;
    inline static constexpr int hopSize = 256;
    
    int getLatencyInSamples() const;
    void reset();
    std::optional<std::array<float, numBins>> processSample(const float& sample);
    void processBlock(float* data, int numSamples);
private:
    std::array<float, numBins> processFrame();
    std::array<float, numBins> getMagnitudeSpectrum(float* data, int numBins);
    
    int count = 0;
    int pos = 0;
    std::array<float, fftSize> inputFifo;
    std::array<float, fftSize> outputFifo;
    std::array<float, fftSize * 2> fftData;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
};
