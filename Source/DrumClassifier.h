#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cstdint>


enum class DrumClass { Kick, Snare, Hat, Unknown };
struct MouthHit;

struct ClassifiedHit
{
    int64_t onsetSample = 0;     // absolute sample index in your recording
    juce::uint8 velocity = 1;    // MIDI velocity 1..127
    DrumClass cls = DrumClass::Unknown;
};

struct DrumClassifierParams
{
    // FFT
    int fftOrder = 10;               // 1024 samples
    int fftOffsetSamples = 0;        // start FFT window at onset + offset (try 0..64)

    // Velocity
    int velocityWindowSamples = 1024; // peak over this many samples after onset
    float velocityScale = 8.0f;       // peakAbs * scale -> 0..1
    float velocityGamma = 0.7f;       // curve (<1 boosts soft hits)

    // Bands for classification (Hz)
    float lowLo = 40.0f,    lowHi = 150.0f;
    float midLo = 150.0f,   midHi = 2000.0f;
    float highLo = 5000.0f, highHi = 12000.0f;

    // Rules (tune)
    float kickLowRatio = 0.55f;
    float kickCentroidHz = 700.0f;

    float hatHighRatio = 0.50f;
    float hatCentroidHz = 4500.0f;
    float hatHighOverMid = 1.80f;   // high must be >= 1.6 * mid to be a hat


    float snareMidRatio = 0.30f;
    float snareCentroidHz = 1300.0f;
};
struct ClassifiedMouthHit
{
    int onsetSample = 0;         // from MouthHit (global timeline)
    juce::uint8 velocity = 1;    // MIDI 1..127
    DrumClass cls = DrumClass::Unknown;
};
class DrumClassifier
{
public:
    DrumClassifier() = default;

    void prepare(double sampleRate, const DrumClassifierParams& params = {});
    void reset(); // (not strictly needed; here for completeness)

    // monoSamples: contiguous mono recording
    // numSamples: length of monoSamples
    // onsetSample: where the hit begins
    ClassifiedHit classifyAt(const float* monoSamples, int numSamples, int64_t onsetSample) const;
    ClassifiedMouthHit classifyMouthHit(const MouthHit& hit, int onsetInHitBuffer = 0) const;


private:
    juce::uint8 peakToMidiVelocity(float peakAbs) const noexcept;

    int hzToBin(float hz) const noexcept;
    float bandEnergy(float hzLo, float hzHi) const noexcept;
    float centroidHz() const noexcept;

private:
    DrumClassifierParams p;
    double sr = 44100.0;

    int fftSize = 1024;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    mutable std::vector<float> fftData;
    mutable std::vector<float> mags;

};

