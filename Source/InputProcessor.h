/*
  ==============================================================================

    InputProcessor.h
    Created: 31 Jan 2026 11:04:40am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#pragma once
#include <array>
#include <JuceHeader.h>
#include "hitClassifier.h"
#include "classifiedHit.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>

struct MouthHit {
    int onsetSample;
    int hitLength;
    juce::AudioBuffer<float> buffer;
    
    HitType type = HitType::Unknown;
};




class ComplexOdf
{
public:
    // fftOrder of 10 (1024 samples) is standard for transient detection
    ComplexOdf(int fftOrder, double sampleRate)
        : fft(fftOrder)
        , window(fft.getSize(), juce::dsp::WindowingFunction<float>::hann)
    {
        auto file = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("fft_magnitudes.txt");

        logFile.open(file.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);
        DBG("ComplexODF Constructor");
        
        fftSize = fft.getSize();
        numBins = fftSize / 2;

        // Allocate workspace for FFT
        fftBuffer.resize(fftSize * 2, 0.0f);

        setupBinWeights(sampleRate);
        
        // Initialize historical arrays
        prevMag1.resize(numBins, 0.0f);
        prevMag2.resize(numBins, 0.0f);
        prevMag3.resize(numBins, 0.0f);
        prevPhase1.resize(numBins, 0.0f);
        prevPhase2.resize(numBins, 0.0f);
    }

    void reset()
    {
        std::fill(prevMag1.begin(), prevMag1.end(), 0.0f);
        std::fill(prevMag2.begin(), prevMag2.end(), 0.0f);
        std::fill(prevMag3.begin(), prevMag3.end(), 0.0f);
        std::fill(prevPhase1.begin(), prevPhase1.end(), 0.0f);
        std::fill(prevPhase2.begin(), prevPhase2.end(), 0.0f);
    }

    void setupBinWeights(double sampleRate)
    {
        binWeights.resize(numBins);
        binWeightSum = 0;
        
        // Frequency spacing per bin = sampleRate / fftSize
        const float binToHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
        const float maxWeight = 10.0f; // The maximum boost at 10 kHz and above

        for (int bin = 0; bin < numBins; ++bin)
        {
            float freq = static_cast<float>(bin) * binToHz;
            
            if (freq < 0) { // potential brickwall highpass filter
                binWeights[bin] = 0;
            } else {
                // 1. Normalize the frequency range [8,000Hz to 10,000Hz] to [0.0 to 1.0]
                // Range width is 2,000Hz
                float t = (freq - 8000.0f) / 2000.0f;
                t = std::clamp(t, 0.0f, 1.0f); // Clamps below 8kHz to 0, and above 10kHz to 1

                // 2. Smoothstep S-curve polynomial
                float s = t * t * (3.0f - 2.0f * t);

                // 3. Map S-curve [0.0, 1.0] to weight range [1.0, maxWeight]
                binWeights[bin] = 1.0f + (maxWeight - 1.0f) * s;
            }
            
            //DBG("bin " << bin << " gets weight of " << binWeights[bin]);
            binWeightSum += binWeights[bin];
        }
    }
    
    // Call this frame-by-frame with a window of size 1024.
    // Typically, you overlap windows (e.g., hop size of 256 or 512 samples).
    float processFrame(const float* sampleWindow, const int currSample)
    {
        // 1. Copy samples and apply the window function
        std::memcpy(fftBuffer.data(), sampleWindow, fftSize * sizeof(float));
        window.multiplyWithWindowingTable(fftBuffer.data(), fftSize);
        fft.performRealOnlyForwardTransform(fftBuffer.data());

        float odfValue = 0.0f;
        const bool writeToFile = logFile.is_open();
        float numBinsReciprocal = 1.0f / static_cast<float>(numBins);
        float distanceScalingFactor = 0;
        const float reciprocalThree = 1.0f / 3.0f;
        
        // --- Pass 1: Calculate raw magnitudes and find the total frame energy ---
        std::vector<float> currentMags(numBins);
        float totalEnergy = 0.0f;

        for (int bin = 0; bin < numBins; ++bin)
        {
            float real = fftBuffer[2 * bin];
            float imag = fftBuffer[2 * bin + 1];
            currentMags[bin] = std::sqrt(real * real + imag * imag);
            totalEnergy += currentMags[bin];
        }
        
        float normFactor = 1.0f / (totalEnergy + 0.05f);
        
        if (writeToFile && false) {
            logFile << std::fixed << std::setprecision(0) << std::setw(10) << currSample << " ";
        }
        
        int changedBinsCount = 0;
        const float noiseFloor = 0.01f;

        // 3. Calculate distance between predicted steady-state and actual complex vector
        for (int bin = 0; bin < numBins; ++bin)
        {
            float mag = currentMags[bin];
            // JUCE FFT output format: interleaved real/imaginary values
            float real = fftBuffer[2 * bin];
            float imag = fftBuffer[2 * bin + 1];
            
            // Fetch the last 3 magnitude instances
            float m1 = prevMag1[bin];
            float m2 = prevMag2[bin];
            float m3 = prevMag3[bin];
            float meanPrev = (m1 + m2 + m3) * reciprocalThree;
            
            // Actual magnitude and phase for current frame
            //float mag = std::sqrt(real * real + imag * imag);
            // Logarithmic compression: log1p(x) calculates ln(1 + x) safely.
            // 1000.0f is the compression factor; higher values boost quiet sounds more.
            //mag = std::log1p(1000.0f * mag);
            float phase = std::atan2(imag, real);

            // --- Write magnitude to file ---
            if (writeToFile)
            {
                // Forces each number to take exactly 10 characters with 4 decimal places
                logFile << std::fixed << std::setprecision(4) << std::setw(10) << mag << " ";
            }
            
            // Fetch historical values
            //float mPrev = prevMag[bin];
            float pPrev1 = prevPhase1[bin]; // Phase at t-1
            float pPrev2 = prevPhase2[bin]; // Phase at t-2

            // Target prediction:
            // 1. Magnitude is assumed constant: Expected mag = mPrev
            // 2. Phase velocity is assumed constant: Expected phase = 2 * pPrev1 - pPrev2
            float targetPhase = 2.0f * pPrev1 - pPrev2;
            targetPhase = fmod(targetPhase + pi, -2 * pi) + pi;
            //float phaseDeviation = phase - expectedPhase;

            // Euclidean distance squared in polar coordinates:
            // |Actual - Expected|^2 = R1^2 + R2^2 - 2*R1*R2*cos(theta1 - theta2)
            //float distance = abs(mPrev - std::polar(mag, phase - targetPhase));
            float distance = abs(meanPrev - mag);
            //const float epsilon = 0.001f; // Prevents division by zero and stabilizes noise floor
            //float distance = std::abs(mag - meanPrev) / (mag + meanPrev + epsilon);
            
            //float distSquared = (mag * mag) + (mPrev * mPrev) - (2.0f * mag * mPrev * std::cos(phaseDeviation));
            
            // Protect against tiny negative values caused by floating-point math
            //float dist = std::sqrt(std::max(0.0f, distSquared));
            
            
            //float binOrdinalityRatio = bin * numBinsReciprocal;
            //float distanceScalingFactor = 1.0f + 4.0f * binOrdinalityRatio;
            // 4. Instant O(1) array lookup for the S-curve weight

            odfValue += distance;
            
            if (mag > noiseFloor || meanPrev > noiseFloor)
            {
                // Ratio calculation
                float ratio = mag / (meanPrev + 1e-5f); // 1e-5 prevents division by zero
                float binContributionFactor = binWeights[bin];
                binContributionFactor = 1;
                
                if (ratio > 1.5f || ratio < 0.6667f)
                {
                    changedBinsCount += binContributionFactor;
                }
                
                //float binFrequencyRatio = static_cast<float>(bin) / static_cast<float>(numBins);
                
                //if ((ratio > 2.0f || ratio < 0.5f) && (binFrequencyRatio < 0.05f)) {
                //    changedBinsCount += 2;
                //}
            }

            // 4. Update phase/magnitude history for the next frame
            prevPhase2[bin] = pPrev1;
            prevPhase1[bin] = phase;
            
            // Update history (shift values back)
            prevMag3[bin] = m2;
            prevMag2[bin] = m1;
            prevMag1[bin] = mag;
        }
        
        float proportion = static_cast<float>(changedBinsCount) / static_cast<float>(numBins);
        float widthMultiplier = 1.0f + 50.0f * proportion;
        //DBG(changedBinsCount);
        //widthMultiplier = binWeights[std::max(changedBinsCount - 1, 0)];
        odfValue *= widthMultiplier;
        
        // End the line for this frame (moving to the next row)
        if (writeToFile)
        {
            logFile << "\n";
        }

        return odfValue;
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    
    int fftSize;
    int numBins;
    
    static constexpr float pi = juce::MathConstants<float>::pi;
    
    std::vector<float> fftBuffer;
    std::vector<float> binWeights;
    float binWeightSum = 0;
    std::ofstream logFile;
    
    // Historical states per frequency bin
    std::vector<float> prevMag1; // t - 1
    std::vector<float> prevMag2; // t - 2
    std::vector<float> prevMag3; // t - 3
    std::vector<float> prevPhase1; // Phase (t-1)
    std::vector<float> prevPhase2; // Phase (t-2)
};

class FastMovingAverage
{
public:
    FastMovingAverage(int windowSize)
        : size(std::max(1, windowSize)), invSize(1.0 / size)
    {
        buffer.resize(size, 0.0f);
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        runningSum = 0.0;
        runningSquareSum = 0.0; // Reset squares
        writeIndex = 0;
        sampleCounter = 0;
        count = size;
    }

    // Call this for every sample. Returns the current average (mean).
    float push(float sample)
    {
        const float oldest = buffer[writeIndex];

        // 1. Update running sum of values
        runningSum -= oldest;
        buffer[writeIndex] = sample;
        runningSum += sample;

        // 2. Update running sum of squared values
        runningSquareSum -= (static_cast<double>(oldest) * oldest);
        runningSquareSum += (static_cast<double>(sample) * sample);

        // 3. Wrap circular index
        writeIndex++;
        if (writeIndex >= size) {
            writeIndex = 0;
        }

        // 4. Periodically clear accumulated float drift for both sums
        sampleCounter++;
        if (sampleCounter >= 2048)
        {
            double exactSum = 0.0;
            double exactSquareSum = 0.0;
            for (float val : buffer) {
                exactSum += val;
                exactSquareSum += (static_cast<double>(val) * val);
            }
            runningSum = exactSum;
            runningSquareSum = exactSquareSum;
            sampleCounter = 0;
        }

        if (count < size) {
            count++;
        }

        // Returns current mean
        return getMean();
    }

    // Get the current Mean - O(1)
    float getMean() const
    {
        if (count == 0) return 0.0f;
        if (count < size) return static_cast<float>(runningSum / count);
        return static_cast<float>(runningSum * invSize);
    }

    // Get the current Variance - O(1)
    float getVariance() const
    {
        const int currentCount = (count < size) ? count : size;
        if (currentCount <= 1) return 0.0f; // Variance of 0 or 1 samples is 0

        const double inv = 1.0 / currentCount;
        const double mean = runningSum * inv;
        const double meanOfSquares = runningSquareSum * inv;

        // Prevent catastrophic cancellation (negative variance)
        return static_cast<float>(std::max(0.0, meanOfSquares - (mean * mean)));
    }

    // Get the Standard Deviation (Square root of Variance) - O(1)
    float getStandardDeviation() const
    {
        return std::sqrt(getVariance());
    }

private:
    int size;
    double invSize;
    std::vector<float> buffer;
    
    double runningSum = 0.0;
    double runningSquareSum = 0.0; // Accumulates squares
    
    int writeIndex = 0;
    int sampleCounter = 0;
    int count = 0;
};

class StatisticalOnsetDetector
{
public:
    StatisticalOnsetDetector(float ratioThreshold, float absoluteThreshold, int baseMeanLength = 1, int mediumHistoryMeanLength = 6, int longHistoryMeanLength = 40)
        : baseSMA(baseMeanLength)              // Tracks immediate energy (Small amt ODF samples)
        , mediumHistoryMeanSMA(mediumHistoryMeanLength)    // Tracks longer historical baseline (5-10x base)
        , longHistoryMeanSMA(longHistoryMeanLength)     // Tracks EVEN longer historical baseline (5-10x medium)
        , ratioThreshold(ratioThreshold)
        , absoluteThreshold(absoluteThreshold)
    {
        reset();
    }

    void reset()
    {
        baseSMA.reset();
        mediumHistoryMeanSMA.reset();
        longHistoryMeanSMA.reset();
        isTentative = false;
        savedMean = 0.0f;
        consecutiveOverCounter = 0;
        prevVariance1 = 0.0f;
        prevVariance2 = 0.0f;
    }

    void setRatioThreshold(float newThreshold)
    {
        ratioThreshold = newThreshold;
    }

    // Pass the current sample amplitude. Returns 'true' on the exact sample the hit is confirmed.
    bool processSample(float amp, int sampleCount)
    {
        // 1. Calculate the current 50-sample average
        float currentSMA = baseSMA.push(amp);
        float mediumHistoryMean = mediumHistoryMeanSMA.push(currentSMA);
        float longHistoryMean = longHistoryMeanSMA.push(currentSMA);
        
        // Get the current variance and fetch the variance from 2 samples ago
        float currentVariance = mediumHistoryMeanSMA.getVariance();
        float varianceTwoSamplesAgo = prevVariance2;
        
        if (false)
        DBG (juce::String::formatted (
            "sample #: %-8d | short-term avg: %-12.4f | long-term avg: %-12.4f | VERY long-term avg: %-12.4f | Variance: %-10.4f",
            sampleCount, currentSMA, mediumHistoryMean, longHistoryMean, mediumHistoryMeanSMA.getVariance()
        ));
        
        bool onsetConfirmed = false;

        if (!isTentative)
        {
            float mediumMeanRatio = currentSMA / mediumHistoryMean;
            float longMeanRatio = currentSMA / longHistoryMean;
            float meanGain = currentSMA - mediumHistoryMean;
            bool longTermTrigger = (mediumMeanRatio > 1.1 && longMeanRatio > 1.4) && (varianceTwoSamplesAgo < 25000);
            
            // If the current average spikes significantly above the running history mean
            if ((mediumMeanRatio > ratioThreshold || longTermTrigger) && (currentSMA > absoluteThreshold))
            {
                isTentative = true;
                savedMean = mediumHistoryMean; // Lock in the baseline mean at the moment of the spike
                consecutiveOverCounter = 1;
            }
        }
        else
        {
            // We are in the 10-sample verification window.
            // Check if the signal stays above the baseline mean we locked in.
            if (currentSMA > savedMean)
            {
                consecutiveOverCounter++;
                
                if (consecutiveOverCounter >= 5)
                {
                    onsetConfirmed = true;
                    isTentative = false; // Reset verification state
                    consecutiveOverCounter = 0;
                }
            }
            else
            {
                // Dropped below the saved baseline mean: false alarm, abort trigger.
                isTentative = false;
                consecutiveOverCounter = 0;
            }
        }
        
        // --- Shift the delay line history at the very end of processing ---
        prevVariance2 = prevVariance1;
        prevVariance1 = currentVariance;

        return onsetConfirmed;
    }
private:
    FastMovingAverage baseSMA;
    FastMovingAverage mediumHistoryMeanSMA;
    FastMovingAverage longHistoryMeanSMA;
    
    float longHistoryThreshold = 1.4f;
    float ratioThreshold;
    float absoluteThreshold;
    bool isTentative = false;
    float savedMean = 0.0f;
    int consecutiveOverCounter = 0;
    
    float prevVariance1 = 0.0f; // Variance from 1 sample ago (z^-1)
    float prevVariance2 = 0.0f; // Variance from 2 samples ago (z^-2)
};


class InputProcessor {
public:
    InputProcessor() {
        // 1. Target the Downloads folder
        auto downloadsDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                            .getChildFile("Downloads");
        juce::File file ("/Users/lightspark/Documents/JuceProjects/HackBrown2026/Data/snarestats.csv");
        //auto file = downloadsDir.getChildFile("drum_features.csv");

        // 2. Check if the file is new before opening it
        const bool isNewFile = !file.exists();

        // 3. Open in Output + Append mode
        csvFile.open(file.getFullPathName().toStdString(), std::ios::out | std::ios::app);

        // 4. Write the header row ONLY if the file was just created
        if (isNewFile && csvFile.is_open())
        {
            csvFile << csvHeader << std::endl;
        }
    };

    /** Where each detected hit is written as its own wav, so the segmentation
        and the classification can be listened to rather than inferred from the
        feature numbers. Sits beside the STFT dump.
    */
    static juce::File getExtractedHitsFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                 .getChildFile("DrumifyAnalysis")
                 .getChildFile("extractedHits");
    }

    /** Empties the folder. Callers do this at the start of a run rather than it
        happening per analysis, so a batch sweep keeps every file's hits.
    */
    static void clearExtractedHits()
    {
        getExtractedHitsFolder().deleteRecursively();
        getExtractedHitsFolder().createDirectory();
    }

    static constexpr const char* csvHeader =
        "FileName,MeanCentroid,Delta,TopEndHeavyCount,LowEndHeavyCount,HighLowDecayRatio,EnergyWeight,TransientZCR";

    /** Points the feature CSV at a different file, replacing whatever was there
        and writing a fresh header. The batch runs use this to give each drum
        type its own file rather than appending them all together.
    */
    void openCsv (const juce::File& file)
    {
        if (csvFile.is_open())
            csvFile.close();

        file.getParentDirectory().createDirectory();
        file.deleteFile();

        csvFile.open (file.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);

        if (csvFile.is_open())
            csvFile << csvHeader << std::endl;
    }
    void activate();
    void deactivate();
    void addSample(float sample);
    void processSample(float sample, float amp, bool externalTrigger);
    void processSampleHFC(	float sample, float amp);
    void initBuffer();
    void reset();
    void flush();
    
    juce::AudioBuffer<float> hitsToBuffer();
    
    HitClassifier hitClassifier;
    int storedHitsIndex = 0;
    void classifyStoredHits(double sampleRate);
    static constexpr int numHits = 64;
    juce::String currFileName;
    std::array<MouthHit, numHits> storedHits;
    // Results live here:
    std::vector<ClassifiedHit> classifiedHits;
    std::ofstream csvFile;
private:
    /*
    static constexpr int numHits = 64;
    const int minOnsetSamples = 512;
    const int minOffsetSamples = 2048;
    const int samplesPerHit = 65536;
    const float ampThreshold = 0.08;
    
    std::array<MouthHit, numHits> storedHits;
    float* writePtr;
    int currHitIndex = 0;
    bool isActivated = false;
    bool isNewBuffer = true; // Creating new buffer before officially activating an onset
    int currOnsetSampleCount = 0;
    int currOffsetSampleCount = 0;
    int currSample = 0;
     */


    // --- Detection parameters ---
    static constexpr float onsetThreshold  = 0.05f;
    static constexpr float offsetThreshold = 0.005f;
    //static constexpr float noveltyThreshold = 0.015f;
    static constexpr float noveltyThreshold = 0.007f;

    static constexpr int minOnsetSamples  = 128;
    static constexpr int minOffsetSamples = 512;

    // --- Buffering ---
    static constexpr int samplesPerHit   = 65536;

    static constexpr int preRollSamples  = 4096;
    float preRoll[preRollSamples] = {};
    int preRollIndex = 0;
    
    // Latency constants
    static constexpr int onsetLatency   = 1500; // Latency of the FFT + ODF + Statistical detector
    static constexpr int padBeforeOnset  = 256;  // Silence cushion before the transient
    static constexpr int totalDelay      = onsetLatency + padBeforeOnset; // 1756 samples total

    float* writePtr = nullptr;

    //int storedHitsIndex = 0;
    int currHitIndex = 0;

    int currSample = 0;
    int onsetCounter = 0;
    int offsetCounter = 0;

    float previousAmp = 0.0f;
    float baselineAmp = 0.0f;
    float trackerSpeed = 0.005f;
    int minHitLength = 512; // In Samples

    bool isActivated = false;
    
    static constexpr float statisticalRatioThreshold = 3.0f; // Adjust this threshold to taste
    static constexpr float statisticalAbsoluteThreshold = 0.15f; // Adjust this threshold to taste
    static constexpr int baseMeanLength = 50; // Adjust this threshold to taste
    static constexpr int historyMeanLength = 1000; // Adjust this threshold to taste
    StatisticalOnsetDetector onsetDetector { statisticalRatioThreshold, statisticalAbsoluteThreshold, baseMeanLength, historyMeanLength };
    
    ComplexOdf complexOnsetDetector{10, 44100};
};
