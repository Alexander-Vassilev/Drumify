#include "hitClassifier.h"
#include "utils.h"
#include <algorithm>

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
        int numSamples = length;
        
        fft.reset();
        
        for (int i = 0; i < numSamples; i++) {
            std::optional<std::array<float, FFTProcessor::numBins>> retVal = fft.processSample(inputData[i]);
            
            if (retVal.has_value()) {
                f.stftData.push_back(*retVal);
            }
        }
    }
    
    return f;
}

// Heuristic classification:
// - Hat: high ZCR + short duration
// - Kick: low ZCR + longer duration + decent RMS
// - Snare: mid ZCR band
HitType HitClassifier::classify(const HitFeatures& f)
{
    DBG("fftActive: " << static_cast<int>(f.fftActive));
    if (f.fftActive) {
        const int movAvgSize = 3;
        const int numWindows = f.stftData.size();
        const int analysisLen = std::min(numWindows, 10);
        std::vector<float> spectralCentroids;
        std::vector<float> prominences;
        std::vector<int> dominantBands;
        
        for (int i = 0; i < analysisLen; i++) {
            std::array<float, numFilters> fftFilterbank = applyMelFilterbank(f.stftData[i], 44100);
                
            for (int j = 0; j < numFilters; j++) {
                std::cout << std::fixed << std::setprecision(1) << std::setw(5) << fftFilterbank[j] << " ";
            }
            std::cout << std::endl;
            
            // 2. Extract features cleanly using the helper class
            float centroid = DrumFeatureExtractor::calculateSpectralCentroid(fftFilterbank);
            float prominence = DrumFeatureExtractor::calculateLowMidProminence(f.stftData[i], 44100, FFTProcessor::numBins);
            
            spectralCentroids.push_back(centroid);
            prominences.push_back(prominence);
        }
        
        PooledHitFeatures pooledFeatures;
        
        // 3. Pool values over time
        float centroidSum = 0.0f;
        float prominenceSum = 0.0f;
        
        for (int i = 0; i < analysisLen; ++i) {
            centroidSum += spectralCentroids[i];
            prominenceSum += prominences[i];
        }
        
        pooledFeatures.meanCentroid = centroidSum / static_cast<float>(analysisLen);
        pooledFeatures.meanLowMidProminence = prominenceSum / static_cast<float>(analysisLen);
        
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
            if (spectralCentroids.size() > 1) {
                pooledFeatures.centroidDelta = spectralCentroids.back() - spectralCentroids.front();
            }
        }
        
        // Print the resulting dynamic footprint
        DBG (juce::String::formatted (
            "Mean Centroid: %-6.2f | Std Dev: %-6.2f | Delta: %-6.2f | Prominence: %-6.2f | ZCR: %-6.2f",
            pooledFeatures.meanCentroid, pooledFeatures.centroidStdDev, pooledFeatures.centroidDelta, pooledFeatures.meanLowMidProminence, f.zcr
        ));
        
        const float kickMaxCentroid = 5.0f;   // Kicks must be concentrated in low bands
        const float kickMaxDelta    = 0.2f;   // Kicks must not shift upwards in pitch

        const float hihatMinCentroid = 12.5f; // Hi-Hats must be concentrated in high bands
        const float hihatMaxStdDev   = 5.0f;  // Hi-Hats are spectrally stable/constant over time

        const float snareMinCentroid = 4.0f;  // Snares must have some mid-range weight
        const float snareMaxCentroid = 14.0f; // Snares should not be as bright as cymbals
        
        const float snareMinProminence = 2.2f;
        
        // ==========================================
        // DECISION TREE
        // ==========================================

        // 1. Check for Hi-Hat / Cymbal Family
        // Bright overall centroid, and spectrally very stable (low standard deviation)
        if (pooledFeatures.meanCentroid >= hihatMinCentroid && pooledFeatures.centroidStdDev <= hihatMaxStdDev && pooledFeatures.meanLowMidProminence < snareMinProminence)
        {
            DBG("Hat");
            return HitType::Hat;
        }

        // 2. Check for Kick Drum Family
        // Dark overall centroid, and doesn't drift upward in pitch
        if (pooledFeatures.meanCentroid <= kickMaxCentroid && pooledFeatures.centroidDelta <= kickMaxDelta)
        {
            DBG("Kick");
            return HitType::Kick;
        }

        // 3. Check for Snare Drum Family
        // Snare sits in the mid-range. Alternatively, if a sound is bright but
        // has a high Std Dev, it is likely a snare rattle rather than a stable hi-hat.
        if (pooledFeatures.meanCentroid >= snareMinCentroid && pooledFeatures.meanCentroid <= snareMaxCentroid)
        {
            DBG("Snare");
            return HitType::Snare;
        }
        else if (pooledFeatures.meanCentroid > snareMaxCentroid && pooledFeatures.centroidStdDev > hihatMaxStdDev)
        {
            DBG("Snare");
            // Bright, but highly unstable over time (the initial crack decaying into snare rattle)
            return HitType::Snare;
        }
        else if (pooledFeatures.meanLowMidProminence >= snareMinProminence)
        {
            DBG("Snare");
            // Bright, but highly unstable over time (the initial crack decaying into snare rattle)
            return HitType::Snare;
        }

        // 4. Fallback if the hit doesn't match standard profiles
        return HitType::Unknown;
        /*
        std::sort(dominantBands.begin(), dominantBands.end());
        
        int mostCommonBand = 0;
        int currBand = dominantBands[0];
        int mostTimesOccurring = 0;
        int currTimesOccurring = 0;
        
        for (auto band : dominantBands) {
            if (band == currBand) {
                currTimesOccurring++;
            } else {
                if (currTimesOccurring > mostTimesOccurring) {
                    mostTimesOccurring = currTimesOccurring;
                    mostCommonBand = currBand;
                }
                
                currBand = band;
                currTimesOccurring = 1;
            }
        }
        
        if (currTimesOccurring > mostTimesOccurring) {
            mostTimesOccurring = currTimesOccurring;
            mostCommonBand = currBand;
        }
        
        float mean = std::accumulate(dominantBands.begin(), dominantBands.end(), 0.0) / dominantBands.size();
        mostCommonBand = mean;
        float dominanceRatio = static_cast<float>(mostTimesOccurring) / numWindows;
        dominanceRatio = 1;
         
        
        DBG("Energy density located at filterbank #" << mostCommonBand);
        DBG("This corresponds to " << bandIndexToHz(mostCommonBand, 44100) << " hz");
        DBG("DominanceRatio: " << dominanceRatio);
        
        if (mostCommonBand > 15 && dominanceRatio > 0.7) {
            DBG("Hat");
            return HitType::Hat;
        } else if (mostCommonBand < 2 && dominanceRatio > 0.8) {
            DBG("Kick");
            return HitType::Kick;
        } else {
            DBG("Snare");
            return HitType::Snare;
        }
         */
        
        
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
