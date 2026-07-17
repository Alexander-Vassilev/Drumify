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
    ComplexOdf(int fftOrder)
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

        // Initialize historical arrays
        prevMag.resize(numBins, 0.0f);
        prevPhase1.resize(numBins, 0.0f);
        prevPhase2.resize(numBins, 0.0f);
    }

    void reset()
    {
        std::fill(prevMag.begin(), prevMag.end(), 0.0f);
        std::fill(prevPhase1.begin(), prevPhase1.end(), 0.0f);
        std::fill(prevPhase2.begin(), prevPhase2.end(), 0.0f);
    }

    // Call this frame-by-frame with a window of size 1024.
    // Typically, you overlap windows (e.g., hop size of 256 or 512 samples).
    float processFrame(const float* sampleWindow, const int currSample)
    {
        // 1. Copy samples and apply the window function
        std::memcpy(fftBuffer.data(), sampleWindow, fftSize * sizeof(float));
        window.multiplyWithWindowingTable(fftBuffer.data(), fftSize);

        // 2. Perform Real-to-Complex FFT (in-place)
        fft.performRealOnlyForwardTransform(fftBuffer.data());

        float odfValue = 0.0f;
        const bool writeToFile = logFile.is_open();
        
        if (writeToFile) {
            logFile << std::fixed << std::setprecision(0) << std::setw(10) << currSample << " ";
        }

        // 3. Calculate distance between predicted steady-state and actual complex vector
        for (int bin = 0; bin < numBins; ++bin)
        {
            // JUCE FFT output format: interleaved real/imaginary values
            float real = fftBuffer[2 * bin];
            float imag = fftBuffer[2 * bin + 1];
            
            // Actual magnitude and phase for current frame
            float mag = std::sqrt(real * real + imag * imag);
            float phase = std::atan2(imag, real);

            // --- Write magnitude to file ---
            if (writeToFile)
            {
                // Forces each number to take exactly 10 characters with 4 decimal places
                logFile << std::fixed << std::setprecision(4) << std::setw(10) << mag << " ";
            }
            
            // Fetch historical values
            float mPrev = prevMag[bin];
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
            float distance = abs(mPrev - mag);
            //float distSquared = (mag * mag) + (mPrev * mPrev) - (2.0f * mag * mPrev * std::cos(phaseDeviation));
            
            // Protect against tiny negative values caused by floating-point math
            //float dist = std::sqrt(std::max(0.0f, distSquared));

            odfValue += distance;

            // 4. Update phase/magnitude history for the next frame
            prevPhase2[bin] = pPrev1;
            prevPhase1[bin] = phase;
            prevMag[bin] = mag;
        }
        
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
    std::ofstream logFile;
    
    // Historical states per frequency bin
    std::vector<float> prevMag;
    std::vector<float> prevPhase1; // Phase (t-1)
    std::vector<float> prevPhase2; // Phase (t-2)
};

class FastMovingAverage
{
public:
    // Initialize with the desired window size
    FastMovingAverage(int windowSize)
        : size(std::max(1, windowSize)), invSize(1.0 / size)
    {
        buffer.resize(size, 0.0f);
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        runningSum = 0.0;
        writeIndex = 0;
        sampleCounter = 0;
    }

    // Call this for every sample. Returns the current average.
    float push(float sample)
    {
        // 1. Subtract the oldest sample leaving the window
        runningSum -= buffer[writeIndex];
        
        // 2. Overwrite with the new sample
        buffer[writeIndex] = sample;
        
        // 3. Add the new sample to the sum
        runningSum += sample;

        // 4. Wrap the circular buffer pointer
        writeIndex++;
        if (writeIndex >= size) {
            writeIndex = 0;
        }

        // 5. Periodically clear accumulated floating-point rounding errors
        sampleCounter++;
        if (sampleCounter >= 2048)
        {
            double exactSum = 0.0;
            for (float val : buffer) {
                exactSum += val;
            }
            runningSum = exactSum;
            sampleCounter = 0;
        }

        // 6. Return the average (multiplication is faster than division)
        return static_cast<float>(runningSum * invSize);
    }

private:
    int size;
    double invSize;
    std::vector<float> buffer;
    double runningSum = 0.0; // Double-precision limits rounding drift
    int writeIndex = 0;
    int sampleCounter = 0;
};

class StatisticalOnsetDetector
{
public:
    StatisticalOnsetDetector(float ratioThreshold, float absoluteThreshold, int baseMeanLength = 50, int historyMeanLength = 100)
        : baseSMA(baseMeanLength)              // Tracks immediate energy (50 samples ~1.1ms at 44.1kHz)
        , historyMeanSMA(historyMeanLength)    // Tracks longer historical baseline (100 averages)
        , ratioThreshold(ratioThreshold)
        , absoluteThreshold(absoluteThreshold)
    {
        reset();
    }

    void reset()
    {
        baseSMA.reset();
        historyMeanSMA.reset();
        isTentative = false;
        savedMean = 0.0f;
        consecutiveOverCounter = 0;
    }

    void setRatioThreshold(float newThreshold)
    {
        ratioThreshold = newThreshold;
    }

    // Pass the current sample amplitude. Returns 'true' on the exact sample the hit is confirmed.
    bool processSample(float amp)
    {
        // 1. Calculate the current 50-sample average
        float currentSMA = baseSMA.push(amp);

        // 2. Calculate the mean of the last 100 averages (running in O(1) time)
        float historyMean = historyMeanSMA.push(currentSMA);
        //DBG("long-term avg: " << historyMean << " short-term avg: " << currentSMA);
        
        bool onsetConfirmed = false;

        if (!isTentative)
        {
            float meanRatio = currentSMA / historyMean;
            
            // If the current average spikes significantly above the running history mean
            if (meanRatio > ratioThreshold && currentSMA > absoluteThreshold)
            {
                isTentative = true;
                savedMean = historyMean; // Lock in the baseline mean at the moment of the spike
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

        return onsetConfirmed;
    }

private:
    FastMovingAverage baseSMA;
    FastMovingAverage historyMeanSMA;
    
    float ratioThreshold;
    float absoluteThreshold;
    bool isTentative = false;
    float savedMean = 0.0f;
    int consecutiveOverCounter = 0;
};


class InputProcessor {
public:
    void activate();
    void deactivate();
    void addSample(float sample);
    void processSample(float sample, float amp);
    void processSampleHFC(	float sample, float amp);
    void initBuffer();
    void reset();
    
    juce::AudioBuffer<float> hitsToBuffer();
    
    HitClassifier hitClassifier;
    int storedHitsIndex = 0;
    void classifyStoredHits(double sampleRate);
    static constexpr int numHits = 64;
    std::array<MouthHit, numHits> storedHits;
    // Results live here:
    std::vector<ClassifiedHit> classifiedHits;
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
    static constexpr float offsetThreshold = 0.025f;
    //static constexpr float noveltyThreshold = 0.015f;
    static constexpr float noveltyThreshold = 0.007f;

    static constexpr int minOnsetSamples  = 128;
    static constexpr int minOffsetSamples = 512;

    // --- Buffering ---
    static constexpr int samplesPerHit   = 65536;
    static constexpr int preRollSamples  = 256;


    float preRoll[preRollSamples] = {};
    int preRollIndex = 0;

    float* writePtr = nullptr;

    //int storedHitsIndex = 0;
    int currHitIndex = 0;

    int currSample = 0;
    int onsetCounter = 0;
    int offsetCounter = 0;

    float previousAmp = 0.0f;
    float baselineAmp = 0.0f;
    float trackerSpeed = 0.005f;
    int minHitLength = 750; // In Samples

    bool isActivated = false;
    
    static constexpr float statisticalRatioThreshold = 3.0f; // Adjust this threshold to taste
    static constexpr float statisticalAbsoluteThreshold = 0.15f; // Adjust this threshold to taste
    static constexpr int baseMeanLength = 50; // Adjust this threshold to taste
    static constexpr int historyMeanLength = 1000; // Adjust this threshold to taste
    StatisticalOnsetDetector onsetDetector { statisticalRatioThreshold, statisticalAbsoluteThreshold, baseMeanLength, historyMeanLength };
    
    ComplexOdf complexOnsetDetector{10};
};
