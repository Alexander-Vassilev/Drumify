#pragma once
#include <JuceHeader.h>
#include <fstream>
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
    float topEndHeavyRatio = 0.0f;
    float lowEndHeavyRatio = 0.0f;
    
    float lowDecayCentroid = 0.0f;  // Center of mass for low decay
    float highDecayCentroid = 0.0f; // Center of mass for high decay
    float decayRatio = 0.0f;        // Low decay divided by High decay
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

// --- 1. Structs for Parametric Distributions ---
struct FeatureDistribution
{
    double mean;
    double stdev;
};

struct FeatureWeights
{
    double centroidWeight = 1.0;
    double deltaWeight    = 1.0;
    double topWeight      = 1.0;
    double lowWeight      = 1.0;
};

struct DrumClassParameters
{
    FeatureDistribution meanCentroid;
    FeatureDistribution delta;
    FeatureDistribution topEndHeavy;
    FeatureDistribution lowEndHeavy;
    
    FeatureWeights weights;
};

struct ClassificationResult
{
    double hatProbability;   // 0.0 to 100.0%
    double kickProbability;  // 0.0 to 100.0%
    double snareProbability; // 0.0 to 100.0%
};

const DrumClassParameters hatParams {
    { 17.5, 1.66 },    // Mean Centroid
    { -0.997, 2.98 },  // Delta
    { 0.970, 0.103 },  // TopEndHeavyRatio
    { 0.117, 0.223 },  // LowEndHeavyRatio
    
    {
        0.0,  // centroidWeight
        1,  // deltaWeight
        1.0,  // topWeight
        0.6   // lowWeight
    }
};

const DrumClassParameters kickParams {
    { 3.75, 2.74 },
    { -9.67, 8.21 },
    { 0.200, 0.286 },
    { 0.967, 0.0579 },
    
    {
        0.6,  // centroidWeight
        1,  // deltaWeight
        0.8,  // topWeight
        1.0   // lowWeight
    }
};

const DrumClassParameters snareParams {
    { 12.8, 2.05 },
    { -7.83, 11.52 },
    { 0.545, 0.392 },
    { 0.494, 0.337 },
    
    {
        1,  // centroidWeight
        1.5,  // deltaWeight
        1.2,  // topWeight
        1.2   // lowWeight
    }
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
    
    static float calculateTemporalCentroid(const std::vector<float>& envelope)
    {
        float sumEnergy = 0.0f;
        float weightedSum = 0.0f;
        
        for (size_t t = 0; t < envelope.size(); ++t)
        {
            float energy = envelope[t];
            weightedSum += static_cast<float>(t) * energy;
            sumEnergy += energy;
        }
        
        // Returns average frame index where the energy lives
        return (sumEnergy > 1e-5f) ? (weightedSum / sumEnergy) : 0.0f;
    }
};

class HitClassifier
{
public:
    HitFeatures extractFeatures(const juce::AudioBuffer<float>& buffer, int length, double sampleRate);
    static std::vector<std::vector<juce::dsp::Complex<float>>> getSTFT(const juce::AudioBuffer<float>& buffer, int length);
    static HitType classify(const HitFeatures& f, std::ofstream& csvFile);
    static const char* toString(HitType t);
    
    FFTProcessor fft;
    static PooledHitFeatures totalFeatures; // To find stats across all hits
private:
    static float computeRMS(const juce::AudioBuffer<float>& buffer, int length);
    static float computeZeroCrossingRate(const juce::AudioBuffer<float>& buffer, int length);
    static float getDelta(const std::vector<float>& values, const std::vector<float>& volumes);
    static double calculateGaussianPDF(double x, double mean, double stdev);
    static double calculateClassLikelihood(const PooledHitFeatures& f, const DrumClassParameters& params);
    static void increaseFeatureCount(PooledHitFeatures& f) {
        HitClassifier::totalFeatures.meanCentroid += f.meanCentroid;
        HitClassifier::totalFeatures.centroidDelta += f.centroidDelta;
        HitClassifier::totalFeatures.topEndHeavyRatio += f.topEndHeavyRatio;
        HitClassifier::totalFeatures.lowEndHeavyRatio += f.lowEndHeavyRatio;
        HitClassifier::totalFeatures.decayRatio += f.decayRatio;
        
        std::cout << "total centroid: " << HitClassifier::totalFeatures.meanCentroid << std::endl;
    }
};
