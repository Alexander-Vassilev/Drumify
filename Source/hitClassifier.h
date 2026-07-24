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

struct PooledHitFeatures
{
    float meanCentroid = 0.0f;
    float centroidStdDev = 0.0f;
    float centroidDelta = 0.0f;
    float meanLowMidProminence = 0.0f;
    int topEndHeavyCount = 0;
    int lowEndHeavyCount = 0;
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

class DrumFeatureExtractor
{
public:
    // Calculates the sharpness of the low-mid resonance
    static float calculateLowMidProminence(const std::array<float, FFTProcessor::numBins>& magnitudes, float sampleRate, int fftSize)
    {
        const int numBins = magnitudes.size();
        const float binToHz = sampleRate / static_cast<float>(fftSize);
        const float noiseFloor = 0.01f; // Ignore silent bins

        int startBin = std::max(2, static_cast<int>(100.0f / binToHz));
        int endBin = std::min(numBins - 3, static_cast<int>(800.0f / binToHz));

        float maxProminence = 1.0f;

        for (int bin = startBin; bin <= endBin; ++bin)
        {
            float mag = magnitudes[bin];

            // Local peak condition
            if (mag > noiseFloor && mag > magnitudes[bin - 1] && mag > magnitudes[bin + 1])
            {
                float localAvg = (magnitudes[bin - 2] + magnitudes[bin - 1] +
                                  magnitudes[bin + 1] + magnitudes[bin + 2]) * 0.25f;

                float prominence = mag / (localAvg + 1e-5f);
                if (prominence > maxProminence) {
                    maxProminence = prominence;
                }
            }
        }

        return maxProminence;
    }

    // Calculates the centroid (brightness) of the filterbank
    template <size_t numFilters>
    static float calculateSpectralCentroid(const std::array<float, numFilters>& filterbank)
    {
        float weightedSum = 0.0f;
        float totalSum = 0.0f;
        
        for (size_t j = 0; j < numFilters; j++)
        {
            float energy = filterbank[j];
            weightedSum += static_cast<float>(j) * energy;
            totalSum += energy;
        }
        
        return (totalSum > 1e-5f) ? (weightedSum / totalSum) : 0.0f;
    }
    
    template <size_t numFilters>
    static float calculateAvgEnergyInBand(const std::array<float, numFilters>& filterbank, int lowBand, int highBand)
    {
        // 1. Clamp both inputs strictly within the valid range [0, numFilters - 1]
        const int maxValidIndex = static_cast<int>(numFilters) - 1;
        int start = std::clamp(lowBand, 0, maxValidIndex);
        int end   = std::clamp(highBand, 0, maxValidIndex);
        
        // 2. If the user passed them backwards (e.g., low = 15, high = 5), swap them
        if (start > end) {
            std::swap(start, end);
        }
        
        float totalSum = 0.0f;
        int totalBins = (end - start) + 1; // No abs() needed now because start <= end is guaranteed
        
        for (int j = start; j <= end; ++j)
        {
            totalSum += filterbank[j];
        }
        
        return (totalSum > 1e-5f) ? (totalSum / static_cast<float>(totalBins)) : 0.0f;
    }
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
    static float getDelta(std::vector<float> centroids);
};
