/*
  ==============================================================================

    InputProcessor.cpp
    Created: 31 Jan 2026 11:04:31am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "InputProcessor.h"

void InputProcessor::Activate() {
    isActivated = true;
    storedHitsIndex++;
    storedHits[storedHitsIndex].setSize(1, samplesPerHit);
    writePtr = storedHits[storedHitsIndex].getWritePointer(0);
};

void InputProcessor::Deactivate() {
    isActivated = false;
};

void InputProcessor::AddSample(float sample) {
    //writePtr[currHitIndex] = sample;
    currHitIndex++;
};
