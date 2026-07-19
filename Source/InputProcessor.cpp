/*
  ==============================================================================

    InputProcessor.cpp
    Created: 31 Jan 2026 11:04:31am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "InputProcessor.h"
#include "hitClassifier.h"
#include "JuceHeader.h"
/*
void InputProcessor::activate() {
    isActivated = true;
};

void InputProcessor::initBuffer() {
    if (isNewBuffer) {
        storedHits[storedHitsIndex].onsetSample = currSample;
        storedHits[storedHitsIndex].buffer.setSize(1, samplesPerHit);
        writePtr = storedHits[storedHitsIndex].buffer.getWritePointer(0);
        currOnsetSampleCount = 0;
        isNewBuffer = false;
    }
}

void InputProcessor::deactivate() {
    storedHits[storedHitsIndex].hitLength = currHitIndex;
    storedHitsIndex++;
    isActivated = false;
    currOffsetSampleCount = 0;
    isNewBuffer = true;
    currHitIndex = 0;
};

void InputProcessor::addSample(float sample) {
    if (currHitIndex < samplesPerHit - 1) {
        writePtr[currHitIndex] = sample;
        currHitIndex++;
    }
};

*/

void InputProcessor::reset()
{
    storedHitsIndex = 0;
    currHitIndex = 0;
    isActivated = false;
    onsetCounter = 0;
    offsetCounter = 0;
    previousAmp = 0.0f;
    baselineAmp = 0;
    
    onsetDetector.reset();
}

void InputProcessor::processSample(float sample, float amp)
{
    if (storedHitsIndex >= numHits)
        return;

    currSample++;

    // --- Update pre-roll buffer (always) ---
    preRoll[preRollIndex] = sample;
    preRollIndex = (preRollIndex + 1) % preRollSamples;

    // This keeps the internal 50 and 100 sample moving averages up-to-date.
    bool isTriggerDetected = onsetDetector.processSample(amp, 0);
    // --- Envelope novelty (onset emphasis) ---
    // --- Envelope novelty (Baseline Tracker) ---
    // The baseline slowly chases the current amplitude
    //baselineAmp += trackerSpeed * (amp - baselineAmp);
    
    // Novelty is how far the current amp has spiked ABOVE the slow baseline
    //float novelty = std::max(0.0f, amp - baselineAmp);

    // ===============================
    // ONSET LOGIC (simplified trigger)
    // ===============================
    if (!isActivated)
    {
        // Trigger on FIRST strong transient, not sustained signal
        if (isTriggerDetected)
        //if (amp > onsetThreshold && novelty > noveltyThreshold)
        {
            DBG("new hit");
            DBG("curr sample hit start: " << currSample);
            // --- Activate hit immediately ---
            isActivated = true;
            currHitIndex = 0;

            auto& hit = storedHits[storedHitsIndex];
            hit.onsetSample = std::max(0, currSample - preRollSamples); // Account for pre-roll
            hit.buffer.setSize(1, samplesPerHit);
            hit.buffer.clear();
            writePtr = hit.buffer.getWritePointer(0);

            // --- Copy pre-roll ---
            for (int i = 0; i < preRollSamples; ++i)
            {
                int idx = (preRollIndex + i) % preRollSamples;
                writePtr[currHitIndex] = preRoll[idx];
                currHitIndex++;
            }
        }
    }
    // ===============================
    // MID-HIT RE-TRIGGER LOGIC
    // ===============================
    else
    {
        // Enforce a lockout period (e.g. 1024 samples is ~23ms at 44.1kHz).
        // This allows the initial transient to fully settle before we begin
        // listening for a second physical hit.
        constexpr int retriggerLockout = 1024;

        if (currHitIndex > retriggerLockout && isTriggerDetected)
        {
            DBG("re-triggered new hit mid-signal (statistical)");
            DBG("curr sample hit start: " << currSample);

            int hitLength = std::max(0, currHitIndex - preRollSamples);
            
            if (hitLength > minHitLength) {
                // 1. Finalize the current active hit, trimming the pre-roll samples.
                auto& oldHit = storedHits[storedHitsIndex];
                oldHit.hitLength = hitLength;
                storedHitsIndex++;
            }

            if (storedHitsIndex >= numHits)
            {
                isActivated = false;
            }
            else
            {
                // 2. Initialize the new hit immediately
                currHitIndex = 0;
                auto& newHit = storedHits[storedHitsIndex];
                newHit.onsetSample = std::max(0, currSample - preRollSamples);
                newHit.buffer.setSize(1, samplesPerHit);
                newHit.buffer.clear();
                writePtr = newHit.buffer.getWritePointer(0);

                // 3. Copy the pre-roll samples into the new hit
                for (int i = 0; i < preRollSamples; ++i)
                {
                    
                    int idx = (preRollIndex + i) % preRollSamples;
                    writePtr[currHitIndex] = preRoll[idx];
                    currHitIndex++;
                }

                offsetCounter = 0; // Reset offset counter for the brand new hit
            }
        }
    }

    // ===============================
    // RECORDING LOGIC
    // ===============================
    if (isActivated) {
        
        if (currHitIndex < samplesPerHit) {
            writePtr[currHitIndex] = sample;
            currHitIndex++;
        }
            
        // ===============================
        // OFFSET LOGIC (sustained below threshold)
        // ===============================
        if (amp < offsetThreshold) {
            offsetCounter++;
            //DBG(offsetCounter);
            if (offsetCounter >= minOffsetSamples) {
                DBG("curr sample hit complete: " << currSample);
                
                if (currHitIndex > minHitLength) {
                    // --- Finalize hit ---
                    auto& hit = storedHits[storedHitsIndex];
                    hit.hitLength = currHitIndex;
                    //DBG("hit length:");
                    //DBG(currHitIndex);

                    storedHitsIndex++;
                    isActivated = false;
                    offsetCounter = 0;
                }
            }
        } else {
            offsetCounter = 0;  // Reset if amplitude goes back up
        }
    }
}

juce::AudioBuffer<float> InputProcessor::hitsToBuffer() {
    int totalSamples = 0;
    const int samplesBetweenHits = 40000;
    
    for (int hit = 0; hit < storedHitsIndex; hit++) {
        totalSamples += storedHits[hit].hitLength + samplesBetweenHits;  // hit + padding
    }
    
    juce::AudioBuffer<float> retBuffer;
    retBuffer.setSize(1, totalSamples);
    float* retBuffWritePtr = retBuffer.getWritePointer(0);
    int currIndex = 0;
    
    for (int hit = 0; hit < storedHitsIndex; hit++) {
        auto* readPtr = storedHits[hit].buffer.getReadPointer(0);
        
        for (int sample = 0; sample < storedHits[hit].hitLength; sample++) {
            retBuffWritePtr[currIndex] = readPtr[sample];
            currIndex++;
        }
        
        for (int padding = 0; padding < samplesBetweenHits; padding++) {
            retBuffWritePtr[currIndex] = 0;
            currIndex++;
        }
    }
    DBG("Hits conveted to bufer");
    
    return retBuffer;
};

void InputProcessor::classifyStoredHits(double sampleRate)
{
    classifiedHits.clear();
    classifiedHits.reserve(storedHitsIndex);

    //DBG("---- Classifying Stored Hits ----");
    //storedHitsIndex = 1;
    
    for (int i = 0; i < storedHitsIndex; ++i)
    {
// ------------------------------ TEMPORARY FILE LOADING TEST SUBSTITUTING MIC ------------------------
        
        /*juce::AudioFormatManager formatManager;
        juce::AudioBuffer<float> sampleBuffer;
        double sampleRate = 44100;
        const float* inputData = nullptr;
        int numSamples;

        formatManager.registerBasicFormats();
        juce::File file("/Users/lightspark/Documents/Image-Line/FL Studio/Projects/5.24.2026-DrumifyPlayground/Noise.wav");
        
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

        if (reader != nullptr) {
            sampleBuffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
            
            reader->read(&sampleBuffer,
                             0,                            // dest start sample
                             (int)reader->lengthInSamples, // num samples to read
                             0,                            // source start sample
                             true,                         // fill left channel
                             true);                        // fill right channel
            inputData = sampleBuffer.getReadPointer(0);
            numSamples = sampleBuffer.getNumSamples();
            DBG("loaded file");
        } else {
            DBG("failed to load file");
        }
        
        MouthHit hit;
        const auto features = hitClassifier.extractFeatures(sampleBuffer, numSamples, sampleRate);*/
// ------------------------------ TEMPORARY FILE LOADING TEST SUBSTITUTING MIC ------------------------
        const auto& hit = storedHits[i];
        const auto features = hitClassifier.extractFeatures(hit.buffer, hit.hitLength, sampleRate);
        
        DBG("STFT window count: " << features.stftData.size());
        
        for (int i = 0; i < 4; i++) {
            //std::cout << "bins begin: ";
            
            for (int j = 0; j < FFTProcessor::numBins; j+= 20) {
                //std::cout << std::trunc(100 * features.stftData[i][j]) / 100 << " ";
            }
            
            //std::cout << "bins end" << std::endl;
        }
        
        ClassifiedHit classified;
        classified.hitIndex = i;
        classified.onsetSample = hit.onsetSample;
        classified.type = HitClassifier::classify(features);
        classified.rms = features.rms;
        classified.zcr = features.zcr;
        classified.durationSec = features.durationSec;
        DBG(" Dur=" << classified.durationSec << "s");

        classifiedHits.push_back(classified);

        //DBG("Hit #" << i
         //   << " -> " << HitClassifier::toString(classified.type)
        //    << " | RMS=" << classified.rms
        //    << " ZCR=" << classified.zcr
       //     << " Dur=" << classified.durationSec << "s");
    }

    //DBG("---- Classification Complete ----");
}


class JuceHfcDetector
{
public:
    // 512 or 1024 is typical for fast transient detection
    JuceHfcDetector(int fftOrder)
        : fft(fftOrder)
        , window(fft.getSize(), juce::dsp::WindowingFunction<float>::hann)
    {
        fftBuffer.resize(fft.getSize() * 2, 0.0f);
    }

    // Call this whenever you have collected a full window of samples (e.g., 512 samples)
    float calculateHfc(const float* sampleWindow)
    {
        // 1. Copy samples and apply Hann window
        std::memcpy(fftBuffer.data(), sampleWindow, fft.getSize() * sizeof(float));
        window.multiplyWithWindowingTable(fftBuffer.data(), fft.getSize());

        // 2. Perform forward FFT (in-place)
        fft.performRealOnlyForwardTransform(fftBuffer.data());

        // fftBuffer now contains interleaved complex numbers: [real0, imag0, real1, imag1, ...]
        float hfc = 0.0f;
        int numBins = fft.getSize() / 2;

        // 3. Sum the weighted magnitude of each bin
        for (int bin = 0; bin < numBins; ++bin)
        {
            float real = fftBuffer[2 * bin];
            float imag = fftBuffer[2 * bin + 1];
            float magnitude = std::sqrt(real * real + imag * imag);

            // Weight linearly by the bin index (HFC formula)
            hfc += magnitude * static_cast<float>(bin);
        }

        return hfc;
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> fftBuffer;
};
