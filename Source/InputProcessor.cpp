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
        storedHitsIndex++;
        currOnsetSampleCount = 0;
        isNewBuffer = false;
    }
}

void InputProcessor::deactivate() {
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
