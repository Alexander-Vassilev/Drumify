/*
  ==============================================================================

    InputProcessor.cpp
    Created: 31 Jan 2026 11:04:31am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "InputProcessor.h"



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
};

void InputProcessor::addSample(float sample) {
    if (currHitIndex < samplesPerHit - 5) {
        writePtr[currHitIndex] = sample;
        currHitIndex++;
    }
};

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
