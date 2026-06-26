/*
  ==============================================================================

    FFTProcessor.cpp
    Created: 24 May 2026 11:27:15am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "FFTProcessor.h"

FFTProcessor::FFTProcessor():
    fft(fftOrder),
    window(fftSize + 1, juce::dsp::WindowingFunction<float>::WindowingMethod::hann, false) {
    
}

std::array<float, FFTProcessor::numBins> FFTProcessor::processFrame() {
    const float* inputPtr = inputFifo.data();
    float* fftPtr = fftData.data();
    
    std::memcpy(fftPtr, inputPtr + pos, (fftSize - pos) * sizeof(float));
    
    if (pos > 0) {
        std::memcpy(fftPtr + fftSize - pos, inputPtr, pos * sizeof(float));
    }
    
    window.multiplyWithWindowingTable(fftPtr, fftSize);
    
    fft.performRealOnlyForwardTransform(fftPtr, true);
    return getMagnitudeSpectrum(fftPtr, numBins);
}

std::array<float, FFTProcessor::numBins> FFTProcessor::getMagnitudeSpectrum(float* data, int numBins) {
    auto* cdata = reinterpret_cast<std::complex<float>*>(data);
    std::array<float, FFTProcessor::numBins> mags {};
    
    for (int i = 0; i < numBins; i++) {
        mags[i] = std::abs(cdata[i]);
    }
    
    return mags;
}

std::optional<std::array<float, FFTProcessor::numBins>> FFTProcessor::processSample(const float& sample) {
    inputFifo[pos] = sample;
    
    float outputSample = outputFifo[pos];
    outputFifo[pos] = 0;
    pos++;
    
    if (pos == fftSize) {
        pos = 0;
    }
    
    count++;
    
    if (count == hopSize) {
        count = 0;
        return processFrame();
    }
    
    return std::nullopt;
}

void FFTProcessor::reset() {
    count = 0;
    pos = 0;
    std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
    std::fill(outputFifo.begin(), outputFifo.end(), 0.0f);
}
