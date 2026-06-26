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
    
    DBG("length: " << length);
    
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
        std::vector<int> dominantBands;
        
        for (int i = 0; i < numWindows; i++) {
            std::array<float, numFilters> fftFilterbank = applyMelFilterbank(f.stftData[i], 44100);
            std::array<float, numFilters - movAvgSize> avgEnergies{};
            
            for (int j = 0; j < numFilters - movAvgSize; j++) {
                float accumulator = 0;
                
                for (int k = 0; k < movAvgSize; k++) {
                    accumulator += fftFilterbank[j + k];
                }
                
                accumulator /= movAvgSize;
                avgEnergies[j] = accumulator;
            }
            
            auto maxIt = std::max_element(avgEnergies.begin(), avgEnergies.end());
            int dominantBand = std::distance(avgEnergies.begin(), maxIt);
            dominantBands.push_back(dominantBand);
        }
        
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
        
        DBG("Energy density located at filterbank #" << mostCommonBand);
        DBG("This corresponds to " << bandIndexToHz(mostCommonBand, 44100) << " hz");
        
        float dominanceRatio = mostTimesOccurring / numWindows;
        
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
