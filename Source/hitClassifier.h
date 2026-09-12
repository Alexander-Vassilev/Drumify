#pragma once
#include <JuceHeader.h>
#include <fstream>
#include "FFTProcessor.h"

// Forward declare to avoid include cycles if you want
struct MouthHit;
static constexpr int stftMaxWindowCount = 300;

// Window the transient ZCR is measured over, and where to start it when the
// envelope follower finds no distinct spike.
static constexpr int transientZcrWindowLength = 700;
static constexpr int transientZcrFallbackStart = 300;

// The most samples the Schmitt-style crossing detector's level averages over.
// The mean is cumulative up to this and rolling beyond it, so a long half-cycle
// cannot bank unbounded inertia against a subsequent run of quiet opposing
// samples. 8 is the smallest value found to still catch a decaying tail.
static constexpr int schmittMeanMemorySamples = 8;

// First mel band the no-bass centroid includes. Bands 0 and 1 hold the sub and
// bass that anchor a kick's overall centroid; skipping them leaves the
// brightness of whatever sits above the fundamental.
static constexpr int noBassCentroidFirstBand = 2;
// Scales every feature weight except ZCR, so a single value shifts how much
// the spectral features say relative to it. Below 1 makes ZCR more decisive;
// above 1 less so - the weights are exponents in a product, so doubling them
// doubles those features' share of the decision in log space.
static constexpr float featureWeightScale = 1.0f;

// ZCR gets the loudest voice: it separates hats from everything else by a
// wide margin, and kicks from snares at their means.
static constexpr float zcrWeight = 2.0f;

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
    float meanCentroidNoBass = 0.0f;  // centroid over mel bands 2 and up
    float centroidStdDev = 0.0f;
    float centroidDelta = 0.0f;
    float meanLowMidProminence = 0.0f;
    float topEndHeavyRatio = 0.0f;
    float lowEndHeavyRatio = 0.0f;
    
    float lowDecayCentroid = 0.0f;  // Center of mass for low decay
    float highDecayCentroid = 0.0f; // Center of mass for high decay
    float decayRatio = 0.0f;        // Low decay divided by High decay

    float deltaEnergyWeight = 0.0f; // The energy term getDelta scales its slope by
    float transientZcr = 0.0f;      // zcr over a fixed window at the transient
};

struct HitFeatures
{
    float rms = 0.0f;
    float zcr = 0.0f;      // zero crossing rate over the whole hit
    float transientZcr = 0.0f;  // zcr over a fixed window at the transient
    float durationSec = 0.0f;
    // FFT data
    bool fftActive = true;
    int windowCount = 0;
    
    std::vector<std::array<float, FFTProcessor::numBins>> stftData{};
};

// --- 1. Structs for Parametric Distributions ---

/** Whether a feature is characteristic of a class at a particular value, or is
    monotonic evidence for it.
*/
enum class FeatureDirection
{
    twoSided,        // the usual bell: both extremes count against the class
    higherIsBetter,  // past the mean is no less characteristic, so no penalty
    lowerIsBetter    // the mirror image: nothing below the mean is penalised
};

struct FeatureDistribution
{
    double mean;
    double stdev;
    FeatureDirection direction = FeatureDirection::twoSided;
};

struct FeatureWeights
{
    double centroidWeight = 1.0;
    double centroidNoBassWeight = 1.0;
    double deltaWeight    = 1.0;
    double topWeight      = 1.0;
    double lowWeight      = 1.0;
    double decayWeight    = 1.0;
    double zcrWeight      = 1.0;
};

struct DrumClassParameters
{
    FeatureDistribution meanCentroid;
    FeatureDistribution centroidNoBass;
    FeatureDistribution delta;
    FeatureDistribution topEndHeavy;
    FeatureDistribution lowEndHeavy;
    FeatureDistribution decayRatio;
    FeatureDistribution transientZcr;

    FeatureWeights weights;
};

struct ClassificationResult
{
    double hatProbability;   // 0.0 to 100.0%
    double kickProbability;  // 0.0 to 100.0%
    double snareProbability; // 0.0 to 100.0%
};

// Means and standard deviations below are measured, not hand-tuned: they come
// from batchAnalyseFolder over Data/Hats (133 hits), Data/Kicks (273) and
// Data/Snares (260). Re-run the sweep and update these together whenever a
// feature's definition changes, or they will describe the old scale.

const DrumClassParameters kickParams {
    { 3.6331,   2.7335 },
    { 8.9270,   3.2634 },   // CentroidNoBass
    { -1.4477,  1.7798 },
    { 0.8077,   0.9236 },
    // Likewise the more the low end dominates, the more kick-like - and this
    // distribution has a long right tail a two-sided bell would punish.
    { 196.2752, 424.9242, FeatureDirection::higherIsBetter },
    { 2.5298,   1.8810 },
    // The mirror of the hat case: a kick cannot cross zero too rarely, so only
    // rates above the mean count against it.
    { 0.0067,   0.0128, FeatureDirection::lowerIsBetter },  // TransientZCR

    {
        featureWeightScale * 0.6,  // centroidWeight
        0.0,                       // centroidNoBassWeight - kicks keep the full centroid, sub and all
        featureWeightScale * 1,    // deltaWeight
        featureWeightScale * 0.05, // topWeight
        featureWeightScale * 1.0,  // lowWeight
        featureWeightScale * 1.0,  // decayWeight - the one feature that cleanly separates kicks
        zcrWeight
    }
};

const DrumClassParameters snareParams {
    { 12.5936, 1.9702 },
    { 13.9228, 1.9845 },  // CentroidNoBass
    { -0.3599, 2.3162 },
    { 1.4982,  1.0838 },
    { 1.5298,  1.4059 },
    { 0.8710,  0.2631 },
    // Two-sided: snares sit between the other two, so straying in either
    // direction is evidence against, not for.
    { 0.0845,  0.0528 },  // TransientZCR

    {
        0.0,                       // centroidWeight - superseded by the no-bass version below
        featureWeightScale * 1,    // centroidNoBassWeight - inherits the old centroid weight
        featureWeightScale * 1.0,  // deltaWeight
        featureWeightScale * 1.6,  // topWeight
        featureWeightScale * 0.7,  // lowWeight
        featureWeightScale * 0.5,  // decayWeight - overlaps the hat distribution
        zcrWeight
    }
};

const DrumClassParameters hatParams {
    { 17.2599, 1.6477 },   // Mean Centroid
    { 17.3860, 1.7578 },   // CentroidNoBass
    { -0.0023, 1.3483 },   // Delta
    // The brighter the hit, the more hat-like - there is no such thing as too
    // much top end here, so values above the mean are not penalised.
    { 14.0007, 23.2583, FeatureDirection::higherIsBetter },  // TopEndHeavyRatio
    { 0.4520,  0.3882 },   // LowEndHeavyRatio
    { 0.9771,  0.1869 },   // HighLowDecayRatio
    // Crossing rate is the clearest separator hats have: 0.38 against 0.09 for
    // snares and 0.02 for kicks. Nothing is too busy to be a hat.
    { 0.3390,  0.1028, FeatureDirection::higherIsBetter },  // TransientZCR

    {
        0.0,                       // centroidWeight - superseded by the no-bass version below
        featureWeightScale * 1.0,  // centroidNoBassWeight - inherits the old centroid weight, which was 0
        featureWeightScale * 1.3,  // deltaWeight
        featureWeightScale * 1.0,  // topWeight
        featureWeightScale * 0.1,  // lowWeight
        featureWeightScale * 0.5,  // decayWeight - hat and snare decay overlap, so weight it lightly
        zcrWeight
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
    static float calculateSpectralCentroid(const std::array<float, numFilters>& filterbank, int lowBand, int highBand)
    {
        const int maxValidIndex = static_cast<int>(numFilters) - 1;
        int start = std::clamp(lowBand, 0, maxValidIndex);
        int end   = std::clamp(highBand, 0, maxValidIndex);
        float weightedSum = 0.0f;
        float totalSum = 0.0f;
        
        for (size_t j = start; j < end; j++)
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

    /** The features a batch run reports mean and standard deviation for, in the
        same order as the CSV's value columns.
    */
    static constexpr int numTrackedFeatures = 8;
    static const char* const trackedFeatureNames[numTrackedFeatures];

    static std::array<double, numTrackedFeatures> toFeatureArray (const PooledHitFeatures& f)
    {
        return { f.meanCentroid, f.centroidDelta, f.topEndHeavyRatio,
                 f.lowEndHeavyRatio, f.decayRatio, f.deltaEnergyWeight, f.transientZcr,
                 f.meanCentroidNoBass };
    }

    /** Running mean and variance per feature, so a batch run can report spread
        without holding every hit in memory. Uses Welford rather than summing
        squares, which loses precision when the mean is far from zero.
    */
    struct FeatureStats
    {
        int count = 0;
        std::array<double, numTrackedFeatures> means {};
        std::array<double, numTrackedFeatures> m2 {};   // sum of squared deviations

        void reset() { *this = {}; }

        void add (const PooledHitFeatures& f)
        {
            const auto values = toFeatureArray (f);
            ++count;

            for (int i = 0; i < numTrackedFeatures; ++i)
            {
                const auto delta = values[i] - means[i];
                means[i] += delta / (double) count;
                m2[i] += delta * (values[i] - means[i]);
            }
        }

        double mean (int i) const { return means[i]; }

        /** Sample standard deviation; needs two hits to mean anything. */
        double stdev (int i) const
        {
            if (count < 2)
                return 0.0;

            const auto variance = m2[i] / (double) (count - 1);
            return variance > 0.0 ? std::sqrt (variance) : 0.0;
        }
    };

    static FeatureStats batchStats;

private:
    static float computeRMS(const juce::AudioBuffer<float>& buffer, int length);
    static float computeZeroCrossingRate(const juce::AudioBuffer<float>& buffer, int length);

    /** Zero crossing rate over [startSample, endSample), counting every sign change. */
    static float computeZeroCrossingRateInRange(const juce::AudioBuffer<float>& buffer,
                                                int startSample, int endSample);

    /** Zero crossing rate over [startSample, endSample) with hysteresis: an
        opposing sample only counts if it clears the mean level of the current
        half-cycle, so noise wobbling around zero is not counted. Smaller opposing
        samples instead erode that mean, so a genuine but quiet change of polarity
        is still picked up once enough of them accumulate.
    */
    static float computeSchmittZeroCrossingRateInRange(const juce::AudioBuffer<float>& buffer,
                                                       int startSample, int endSample);

    /** Where the hit's transient begins, found with an envelope follower over the
        already-extracted hit. Returns transientZcrFallbackStart when the envelope
        never rises - a hit segmented out of a sustained passage is already loud at
        sample 0, so there is no spike to lock onto.
    */
    static int findTransientStart(const juce::AudioBuffer<float>& buffer, int length, double sampleRate);
    /** Returns the energy-weighted 6-frame centroid slope. The unweighted
        energy term is reported through `energyWeightOut` when supplied, so it
        can be profiled in its own right.
    */
    static float getDelta(const std::vector<float>& values, const std::vector<float>& volumes, const std::vector<float>& avgEnergies,
                          float* energyWeightOut = nullptr);
    static double calculateGaussianPDF(double x, const FeatureDistribution& dist);
    static double calculateClassLikelihood(const PooledHitFeatures& f, const DrumClassParameters& params);
    static void increaseFeatureCount(PooledHitFeatures& f) {
        HitClassifier::totalFeatures.meanCentroid += f.meanCentroid;
        HitClassifier::totalFeatures.centroidDelta += f.centroidDelta;
        HitClassifier::totalFeatures.topEndHeavyRatio += f.topEndHeavyRatio;
        HitClassifier::totalFeatures.lowEndHeavyRatio += f.lowEndHeavyRatio;
        HitClassifier::totalFeatures.decayRatio += f.decayRatio;
        HitClassifier::totalFeatures.deltaEnergyWeight += f.deltaEnergyWeight;
        HitClassifier::totalFeatures.transientZcr += f.transientZcr;
        HitClassifier::totalFeatures.meanCentroidNoBass += f.meanCentroidNoBass;

        // Every hit that gets a CSV row also lands here, so a batch run can
        // summarise exactly the rows it appended.
        HitClassifier::batchStats.add (f);

        std::cout << "total centroid: " << HitClassifier::totalFeatures.meanCentroid << std::endl;
    }
};
