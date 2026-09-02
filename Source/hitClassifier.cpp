#include "hitClassifier.h"
#include "utils.h"
#include <algorithm>

PooledHitFeatures HitClassifier::totalFeatures {};
HitClassifier::FeatureStats HitClassifier::batchStats {};

const char* const HitClassifier::trackedFeatureNames[HitClassifier::numTrackedFeatures]
{
    "MeanCentroid", "Delta", "TopEndHeavyRatio", "LowEndHeavyRatio", "HighLowDecayRatio",
    "EnergyWeight"
};

float HitClassifier::computeRMS(const juce::AudioBuffer<float>& buffer, int length)
{
    if (length <= 0 || buffer.getNumSamples() <= 0)
        return 0.0f;

    length = juce::jmin(length, buffer.getNumSamples());

    const float* data = buffer.getReadPointer(0);
    double sumSq = 0.0;

    for (int i = 0; i < length; ++i)
        sumSq += (double)data[i] * (double)data[i];

    return (float)std::sqrt(sumSq / (double)length);
}

float HitClassifier::computeZeroCrossingRate(const juce::AudioBuffer<float>& buffer, int length)
{
    if (length <= 1 || buffer.getNumSamples() <= 1)
        return 0.0f;

    length = juce::jmin(length, buffer.getNumSamples());

    const float* data = buffer.getReadPointer(0);
    int crossings = 0;

    for (int i = 1; i < length; ++i)
    {
        const float a = data[i - 1];
        const float b = data[i];

        if ((a >= 0.0f && b < 0.0f) || (a < 0.0f && b >= 0.0f))
            crossings++;
    }

    return (float)crossings / (float)length;
}

/*std::vector<std::vector<juce::dsp::Complex<float>>> HitClassifier::getSTFT(const juce::AudioBuffer<float>& buffer, int length) {
    for (int i = 0; i < length - fftSize; i += hopSize) {
        
    }
}*/

HitFeatures HitClassifier::extractFeatures(const juce::AudioBuffer<float>& buffer, int length, double sampleRate)
{
    HitFeatures f;
    f.rms = computeRMS(buffer, length);
    f.zcr = computeZeroCrossingRate(buffer, length);
    f.durationSec = (sampleRate > 0.0) ? (float)length / (float)sampleRate : 0.0f;
    
    DBG("length: " << f.durationSec);
    
    if (length < 1025) {
        f.fftActive = false;
    } else {
        auto* inputData = buffer.getReadPointer(0);
        int numSamples = juce::jmin(length, buffer.getNumSamples());

        // Scale the hit up to 0 dBFS for the FFT only, so a quiet sample and a
        // loud one of the same drum yield the same spectrum. Applied on the way
        // into the FFT rather than to the buffer, which is const and shared.
        // f.rms above deliberately keeps the original level - it drives velocity.
        const float peak = buffer.getMagnitude(0, 0, numSamples);
        const float normalisingGain = peak > 0.0f ? 1.0f / peak : 1.0f;

        fft.reset();

        for (int i = 0; i < numSamples; i++) {
            std::optional<std::array<float, FFTProcessor::numBins>> retVal
                = fft.processSample(inputData[i] * normalisingGain);

            if (retVal.has_value()) {
                f.stftData.push_back(*retVal);
            }
        }
    }
    
    return f;
}

float HitClassifier::getDelta(const std::vector<float>& values, const std::vector<float>& volumes, const std::vector<float>& avgEnergies,
                              float* energyWeightOut)
{
    const int intendedFrameCount = 6;
    static constexpr float scalingFactor = 10.0f;

    if (energyWeightOut != nullptr)
        *energyWeightOut = 0.0f;

    // 1. Guard: Ensure vectors are valid, match in size, and have at least 6 frames
    const int numFrames = static_cast<int>(values.size());
    if (numFrames < intendedFrameCount || volumes.size() != values.size())
        return 0.0f;

    // 2. Find the peak of the volume (energy) in the first 9 frames
    int searchLimit = std::min(numFrames, 9);
    auto maxIt = std::max_element(volumes.begin(), volumes.begin() + searchLimit);
    int peakIndex = static_cast<int>(std::distance(volumes.begin(), maxIt));
    
    // 3. Clamp peakIndex so we can always fit exactly 6 frames [C++17 std::clamp]
    // The maximum possible starting index is (numFrames - 6)
    peakIndex = std::clamp(peakIndex, 0, numFrames - intendedFrameCount);
    DBG("start index for delta finding: " << peakIndex);
    // Constant parameters for exactly 6 iterations
    const float M = static_cast<float>(intendedFrameCount);
    const int loopEnd = peakIndex + intendedFrameCount;

    float sumX  = 0.0f;
    float sumY  = 0.0f;
    float sumXY = 0.0f;
    float sumXX = 0.0f;
    float sumEnergies = 0.0f;

    // 4. Loop runs exactly 6 times starting at peakIndex
    for (int i = peakIndex; i < loopEnd; ++i)
    {
        float x = static_cast<float>(i - peakIndex); // x goes from 0.0 to 5.0
        float y = values[i];
        sumEnergies += avgEnergies[i];

        DBG("value for delta: " << y << " at adjusted index x: " << x << " (original index: " << i << ")");
        
        sumX  += x;
        sumY  += y;
        sumXY += x * y;
        sumXX += x * x;
    }

    int numIter = loopEnd - peakIndex;
    sumEnergies /= static_cast<float>(numIter);
    // --- Mathematical DSP Insight ---
    // Because M is fixed at 6, and x is always [0, 1, 2, 3, 4, 5]:
    // - sumX is always 15.0f
    // - sumXX is always 55.0f
    // - denominator is always: (6 * 55) - (15 * 15) = 330 - 225 = 105.0f
    float denominator = (M * sumXX) - (sumX * sumX);
    
    if (std::abs(denominator) > 1e-5f)
    {
        float result = ((M * sumXY) - (sumX * sumY)) / denominator;
        float energyWeight = 2 * (std::log(sumEnergies) - 2.9);
        DBG("Dynamic start index: " << peakIndex << " | 6-frame delta: " << result);
        DBG("Delta weight from total energy: " << energyWeight);

        if (energyWeightOut != nullptr)
            *energyWeightOut = energyWeight;

        return scalingFactor * result;
        //return scalingFactor * result * energyWeight;
    }
    
    return 0.0f;
}

double HitClassifier::calculateGaussianPDF(double x, double mean, double stdev)
{
    // Avoid division by zero
    if (stdev <= 0.0)
        stdev = 1e-5;
    
    double exponent = -std::pow(x - mean, 2.0) / (2.0 * std::pow(stdev, 2.0));
    double coefficient = 1.0 / (stdev * std::sqrt(2.0 * juce::MathConstants<double>::pi));
    
    return coefficient * std::exp(exponent);
}

double HitClassifier::calculateClassLikelihood(const PooledHitFeatures& f, const DrumClassParameters& params)
{
    double pCentroid = calculateGaussianPDF(f.meanCentroid,     params.meanCentroid.mean, params.meanCentroid.stdev);
    double pDelta    = calculateGaussianPDF(f.centroidDelta,    params.delta.mean,        params.delta.stdev);
    double pTop      = calculateGaussianPDF(f.topEndHeavyRatio, params.topEndHeavy.mean,  params.topEndHeavy.stdev);
    double pLow      = calculateGaussianPDF(f.lowEndHeavyRatio, params.lowEndHeavy.mean,  params.lowEndHeavy.stdev);

    pCentroid = std::min(1.0, pCentroid);
    pDelta    = std::min(1.0, pDelta);
    pTop      = std::min(1.0, pTop);
    pLow      = std::min(1.0, pLow);
    
    DBG (juce::String::formatted (
        "pCentroid: %-6.2f | pDelta: %-6.2f | pTopEndHeavyRatio: %-6.2f | pLowEndHeavyRatio: %-6.2f",
        pCentroid, pDelta, pTop, pLow
    ));
    
    double wCentroid = std::pow(pCentroid, params.weights.centroidWeight);
    double wDelta    = std::pow(pDelta,    params.weights.deltaWeight);
    double wTop      = std::pow(pTop,      params.weights.topWeight);
    double wLow      = std::pow(pLow,      params.weights.lowWeight);
    
    // Naive Bayes Assumption: Multiply the independent feature probabilities together
    return wCentroid * wDelta * wTop * wLow;
    //return pCentroid * pDelta * pTop * pLow;
}

// Heuristic classification:
// - Hat: high ZCR + short duration
// - Kick: low ZCR + longer duration + decent RMS
// - Snare: mid ZCR band
HitType HitClassifier::classify(const HitFeatures& f, std::ofstream& csvFile)
{
    DBG("fftActive: " << static_cast<int>(f.fftActive));
    if (f.fftActive) {
        const int movAvgSize = 3;
        const int numWindows = f.stftData.size();
        int analysisLen = std::min(numWindows, 28);
        std::vector<float> spectralCentroids;
        std::vector<float> lowMidCentroids;
        std::vector<float> avgLowEnergies;
        std::vector<float> avgLowMidEnergies;
        std::vector<float> avgMidEnergies;
        std::vector<float> avgMidHighEnergies;
        std::vector<float> avgHighEnergies;
        std::vector<float> prominences;
        std::vector<float> totalEnergies;
        std::vector<int> dominantBands;
        
        for (int i = 0; i < analysisLen; i++) {
            std::array<float, numFilters> fftFilterbank = applyMelFilterbank(f.stftData[i], 44100);
            
            float totalEnergy = 0.0f;
            
            for (int j = 0; j < numFilters; j++) totalEnergy += fftFilterbank[j];
            
            const float silenceThreshold = 10.0f;
            
            if (totalEnergy < silenceThreshold) continue;
            
            for (int j = 0; j < numFilters; j++)
                std::cout << std::fixed << std::setprecision(1) << std::setw(5) << fftFilterbank[j] << " ";
            std::cout << std::endl;
            
            // 2. Extract features cleanly using the helper class
            float centroid = DrumFeatureExtractor::calculateSpectralCentroid(fftFilterbank, 0, numFilters);
            float lowMidCentroid = DrumFeatureExtractor::calculateSpectralCentroid(fftFilterbank, 0, 8);
            float prominence = DrumFeatureExtractor::calculateLowMidProminence(f.stftData[i], 44100, FFTProcessor::numBins);
            float lowEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 0, 3);
            float lowMidEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 0, 5);
            float midEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 5, 12);
            float midHighEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 14, 23);
            float highEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 17, 25);
            
            spectralCentroids.push_back(centroid);
            lowMidCentroids.push_back(lowMidCentroid);
            prominences.push_back(prominence);
            avgLowEnergies.push_back(lowEnergy);
            avgLowMidEnergies.push_back(lowMidEnergy);
            avgMidEnergies.push_back(midEnergy);
            avgMidHighEnergies.push_back(midHighEnergy);
            avgHighEnergies.push_back(highEnergy);
            totalEnergies.push_back(totalEnergy);
        }
        
        analysisLen = spectralCentroids.size();
        
        PooledHitFeatures pooledFeatures;
        
        // 3. Pool values over time
        float centroidSum = 0.0f;
        float prominenceSum = 0.0f;
        int topEndHeavyCount = 0;
        int lowEndHeavyCount = 0;
        const int numFramesConsiderTopEndHeavy = std::min(9, analysisLen);
        const int numFramesConsiderLowEndHeavy = std::min(30, analysisLen);
        
        for (int i = 0; i < analysisLen; ++i) {
            centroidSum += spectralCentroids[i];
            prominenceSum += prominences[i];
            //DBG("Low avg nrg: " << avgLowEnergies[i]);
            //DBG("Mid avg nrg: " << avgMidEnergies[i]);
            //DBG("High avg nrg: " << avgHighEnergies[i]);
            
            // Only considering first 8 windows
            if ((avgHighEnergies[i] > avgMidEnergies[i]) && (i < numFramesConsiderTopEndHeavy)) topEndHeavyCount++;
            if ((avgLowEnergies[i] > avgMidEnergies[i]) && (i < numFramesConsiderLowEndHeavy)) lowEndHeavyCount++;
        }
        
        pooledFeatures.meanCentroid = centroidSum / static_cast<float>(analysisLen);
        pooledFeatures.meanLowMidProminence = prominenceSum / static_cast<float>(analysisLen);
        pooledFeatures.topEndHeavyRatio = static_cast<float>(topEndHeavyCount) / static_cast<float>(numFramesConsiderTopEndHeavy);
        pooledFeatures.lowEndHeavyRatio = static_cast<float>(lowEndHeavyCount) / static_cast<float>(numFramesConsiderLowEndHeavy);
        
        if (!spectralCentroids.empty())
        {
            // A. Calculate Mean (Overall Brightness)
            float sum = 0.0f;
            for (float c : spectralCentroids) {
                sum += c;
            }
            pooledFeatures.meanCentroid = sum / static_cast<float>(spectralCentroids.size());

            // B. Calculate Standard Deviation (Spectral Stability over time)
            float varianceSum = 0.0f;
            for (float c : spectralCentroids) {
                float diff = c - pooledFeatures.meanCentroid;
                varianceSum += diff * diff;
            }
            pooledFeatures.centroidStdDev = std::sqrt(varianceSum / static_cast<float>(spectralCentroids.size()));

            // C. Calculate Delta (Spectral Shift Direction: End - Start)
            //pooledFeatures.centroidDelta = getDelta(spectralCentroids);
            pooledFeatures.centroidDelta = getDelta(lowMidCentroids, totalEnergies, avgLowMidEnergies,
                                                    &pooledFeatures.deltaEnergyWeight);
            
            pooledFeatures.lowDecayCentroid  = DrumFeatureExtractor::calculateTemporalCentroid(avgLowEnergies);
            pooledFeatures.highDecayCentroid = DrumFeatureExtractor::calculateTemporalCentroid(avgMidHighEnergies);
            
            pooledFeatures.decayRatio = pooledFeatures.lowDecayCentroid / (pooledFeatures.highDecayCentroid + 1e-5f);
            
            DBG (juce::String::formatted (
                "Decay -> Lows: %-5.2f | Highs: %-5.2f | Ratio (L/H): %-5.2f",
                pooledFeatures.lowDecayCentroid,
                pooledFeatures.highDecayCentroid,
                pooledFeatures.decayRatio
            ));
        }
        
        // Print the resulting dynamic footprint
        DBG (juce::String::formatted (
            "Mean Centroid: %-6.2f | Delta: %-6.2f | TopEndHeavyCount: %-6.2f | LowEndHeavyCount: %-6.2f | HighLowDecayRatio: %-6.2f",
            pooledFeatures.meanCentroid,
            pooledFeatures.centroidDelta,
            pooledFeatures.topEndHeavyRatio,
            pooledFeatures.lowEndHeavyRatio,
            pooledFeatures.decayRatio
        ));
        
        increaseFeatureCount(pooledFeatures);
        
        if (csvFile.is_open())
        {
            // Write each column separated by a comma, ending with std::endl to flush to disk
            csvFile << std::fixed << std::setprecision(4) << ","
                    << pooledFeatures.meanCentroid << ","
                    << pooledFeatures.centroidDelta << ","
                    << pooledFeatures.topEndHeavyRatio << ","
                    << pooledFeatures.lowEndHeavyRatio << ","
                    << pooledFeatures.decayRatio << ","
                    << pooledFeatures.deltaEnergyWeight << std::endl;
        }
        
        DBG("Hat Probabilities:");
        DBG("Kick Probabilities:");
        DBG("Snare Probabilities:");
        
        double hatLikelihood   = calculateClassLikelihood(pooledFeatures, hatParams);
        double kickLikelihood  = calculateClassLikelihood(pooledFeatures, kickParams);
        double snareLikelihood = calculateClassLikelihood(pooledFeatures, snareParams);

        double totalLikelihood = hatLikelihood + kickLikelihood + snareLikelihood;

        ClassificationResult result;

        // ========================================================
        // NORMALIZATION (Convert to percentages)
        // ========================================================
        if (totalLikelihood > 1e-25) // Prevent division by near-zero underflows
        {
            result.hatProbability   = (hatLikelihood / totalLikelihood) * 100.0;
            result.kickProbability  = (kickLikelihood / totalLikelihood) * 100.0;
            result.snareProbability = (snareLikelihood / totalLikelihood) * 100.0;
        }
        else
        {
            // Fallback for extreme outlier signals (assign equal probability)
            result.hatProbability   = 33.33;
            result.kickProbability  = 33.33;
            result.snareProbability = 33.33;
        }
        
        DBG("");
        DBG("Hat Likelihood: " << result.hatProbability);
        DBG("Kick Likelihood: " << result.kickProbability);
        DBG("Snare Likelihood: " << result.snareProbability);

        // Determine the class with the highest probability
        if (result.hatProbability >= result.kickProbability && result.hatProbability >= result.snareProbability)
            return HitType::Hat;
        else if (result.kickProbability >= result.hatProbability && result.kickProbability >= result.snareProbability)
            return HitType::Kick;
        else
            return HitType::Snare;
    }
    
    // Hat: bright/noisy and short
    if (f.zcr > 0.15f) //&& f.durationSec < 0.12f
        return HitType::Hat;

    // Kick: low-frequency dominant, usually longer
    if (f.zcr < 0.08f && f.rms > 0.015f) //&& f.durationSec > 0.15f
        return HitType::Kick;

    // Snare: in-between; often noisy but not as high ZCR as hats
    if (f.zcr >= 0.08f && f.zcr <= 0.15f)
        return HitType::Snare;

    return HitType::Unknown;
}

const char* HitClassifier::toString(HitType t)
{
    switch (t)
    {
        case HitType::Kick:   return "Kick";
        case HitType::Snare:  return "Snare";
        case HitType::Hat:    return "Hat";
        default:              return "Unknown";
    }
}
