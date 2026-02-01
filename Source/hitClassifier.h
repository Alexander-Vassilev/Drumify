#pragma once
#include <JuceHeader.h>

// Forward declare to avoid include cycles if you want
struct MouthHit;

enum class HitType
{
    Kick = 36,
    Snare = 38,
    Hat = 42,
    Unknown = 43
};

struct HitFeatures
{
    float rms = 0.0f;
    float zcr = 0.0f;      // zero crossing rate
    float durationSec = 0.0f;
};

class HitClassifier
{
public:
    static HitFeatures extractFeatures(const juce::AudioBuffer<float>& buffer, int length, double sampleRate);
    static HitType classify(const HitFeatures& f);

    static const char* toString(HitType t);

private:
    static float computeRMS(const juce::AudioBuffer<float>& buffer, int length);
    static float computeZeroCrossingRate(const juce::AudioBuffer<float>& buffer, int length);
};
