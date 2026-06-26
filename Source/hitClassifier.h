#pragma once
#include <JuceHeader.h>
#include "FFTProcessor.h"

// Forward declare to avoid include cycles if you want
struct MouthHit;
static constexpr int stftMaxWindowCount = 300;

enum class HitType
{
    Kick = 36,
    Snare = 38,
    Hat = 42,
    Unknown = 43
};

struct HitFeatures
{
    float rms = 0.0f;
    float zcr = 0.0f;      // zero crossing rate
    float durationSec = 0.0f;
    // FFT data
    bool fftActive = true;
    int windowCount = 0;
    
    std::vector<std::array<float, FFTProcessor::numBins>> stftData{};
};


class HitClassifier
{
public:
    HitFeatures extractFeatures(const juce::AudioBuffer<float>& buffer, int length, double sampleRate);
    static std::vector<std::vector<juce::dsp::Complex<float>>> getSTFT(const juce::AudioBuffer<float>& buffer, int length);
    static HitType classify(const HitFeatures& f);
    static const char* toString(HitType t);
    
    FFTProcessor fft;
private:
    static float computeRMS(const juce::AudioBuffer<float>& buffer, int length);
    static float computeZeroCrossingRate(const juce::AudioBuffer<float>& buffer, int length);
};
