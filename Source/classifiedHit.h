#pragma once
#include <JuceHeader.h>

struct ClassifiedHit
{
    int hitIndex = -1;          // index into storedHits
    int onsetSample = 0;        // sample where hit begins
    HitType type = HitType::Unknown;

    // Optional but useful
    float rms = 0.0f;
    float zcr = 0.0f;
    float durationSec = 0.0f;
};
