/*
  ==============================================================================

    InputProcessor.cpp
    Created: 31 Jan 2026 11:04:31am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "InputProcessor.h"
#include "ClassifiedHit.h"
#include "hitClassifier.h"
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
}

void InputProcessor::processSample(float sample, float amp)
{
    if (storedHitsIndex >= numHits)
        return;

    currSample++;

    // --- Update pre-roll buffer (always) ---
    preRoll[preRollIndex] = sample;
    preRollIndex = (preRollIndex + 1) % preRollSamples;

    // --- Envelope novelty (onset emphasis) ---
    float novelty = amp - previousAmp;
    previousAmp = amp;

    // ===============================
    // ONSET LOGIC (simplified trigger)
    // ===============================
    if (!isActivated)
    {
        // Trigger on FIRST strong transient, not sustained signal
        if (amp > onsetThreshold && novelty > noveltyThreshold)
        {
            // --- Activate hit immediately ---
            isActivated = true;
            currHitIndex = 0;

            auto& hit = storedHits[storedHitsIndex];
            hit.onsetSample = currSample - preRollSamples; // Account for pre-roll
            hit.buffer.setSize(1, samplesPerHit);
            hit.buffer.clear();
            writePtr = hit.buffer.getWritePointer(0);

            // --- Copy pre-roll ---
            for (int i = 0; i < preRollSamples; ++i)
            {
                int idx = (preRollIndex + i) % preRollSamples;
                writePtr[currHitIndex++] = preRoll[idx];
            }
        }
    }

    // ===============================
    // RECORDING LOGIC
    // ===============================
    if (isActivated)
    {
        if (currHitIndex < samplesPerHit)
            writePtr[currHitIndex++] = sample;

        // ===============================
        // OFFSET LOGIC (sustained below threshold)
        // ===============================
        if (amp < offsetThreshold)
        {
            offsetCounter++;

            if (offsetCounter >= minOffsetSamples)
            {
                // --- Finalize hit ---
                auto& hit = storedHits[storedHitsIndex];
                hit.hitLength = currHitIndex;

                storedHitsIndex++;
                isActivated = false;
                offsetCounter = 0;
            }
        }
        else
        {
            offsetCounter = 0;  // Reset if amplitude goes back up
        }
    }
}

/*
void InputProcessor::processSample(float sample, float amp) {
    if (storedHitsIndex < numHits) {
        currSample++;
        
        if (amp > ampThreshold) {
            if (isActivated) {
                addSample(sample);
            } else {
                initBuffer();
                currOnsetSampleCount++;
                addSample(sample);
                
                if (currOnsetSampleCount > minOnsetSamples) {
                    activate();
                    currOnsetSampleCount = 0;
                }
            }
        } else {
            if (isActivated) {
                addSample(sample);
                currOffsetSampleCount++;
                
                if (currOffsetSampleCount > minOffsetSamples) {
                    deactivate();
                }
            } else if (currOnsetSampleCount > 0) {
                currOnsetSampleCount = 0;
                storedHits[storedHitsIndex].buffer.clear();
            }
        }
    }
}
 */
//void InputProcessor::classifyStoredHits(double sampleRate)
//{
    // storedHitsIndex is how many valid hits you have
//    for (int i = 0; i < storedHitsIndex; ++i)
 //   {
 //       auto& hit = storedHits[i];
 //       const auto features = HitClassifier::extractFeatures(hit.buffer, hit.hitLength, sampleRate);
 //       hit.type = HitClassifier::classify(features);

    
 //   }
//}
juce::AudioBuffer<float> InputProcessor::hitsToBuffer() {
    int totalSamples = 0;
    for (int hit = 0; hit < storedHitsIndex; hit++) {
        totalSamples += storedHits[hit].hitLength + 44100;  // hit + padding
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
        
        for (int padding = 0; padding < 44100; padding++) {
            retBuffWritePtr[currIndex] = 0;
            currIndex++;
        }
    }
    
    return retBuffer;
};

void InputProcessor::classifyStoredHits(double sampleRate)
{
    classifiedHits.clear();
    classifiedHits.reserve(storedHitsIndex);

    //DBG("---- Classifying Stored Hits ----");

    for (int i = 0; i < storedHitsIndex; ++i)
    {
        const auto& hit = storedHits[i];

        const auto features =
            HitClassifier::extractFeatures(hit.buffer, hit.hitLength, sampleRate);

        ClassifiedHit classified;
        classified.hitIndex = i;
        classified.type = HitClassifier::classify(features);
        classified.rms = features.rms;
        classified.zcr = features.zcr;
        classified.durationSec = features.durationSec;

        classifiedHits.push_back(classified);

        //DBG("Hit #" << i
         //   << " -> " << HitClassifier::toString(classified.type)
        //    << " | RMS=" << classified.rms
        //    << " ZCR=" << classified.zcr
       //     << " Dur=" << classified.durationSec << "s");
    }

    //DBG("---- Classification Complete ----");
}
