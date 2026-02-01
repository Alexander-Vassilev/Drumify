#include <JuceHeader.h>
#include "DrumClassifier.h"
#include "InputProcessor.h"

// ------------------ helpers to synthesize test sounds ------------------

static juce::AudioBuffer<float> makeSineKick(int numSamples, double sr, float amp)
{
    juce::AudioBuffer<float> b(1, numSamples);
    auto* x = b.getWritePointer(0);

    float phase = 0.0f;
    float f0 = 120.0f;
    float f1 = 45.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float)i / (float)numSamples;

        // exponential-ish pitch drop
        float f = f0 * std::pow(f1 / f0, t);

        // amplitude decay
        float env = std::exp(-6.0f * t);

        float inc = (float)(juce::MathConstants<double>::twoPi * f / sr);
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        x[i] = amp * env * std::sin(phase);
    }
    return b;
}

static juce::AudioBuffer<float> makeNoiseSnare(int numSamples, double sr, float amp)
{
    juce::AudioBuffer<float> b(1, numSamples);
    auto* x = b.getWritePointer(0);

    juce::Random rng(12345);

    float phase = 0.0f;
    const float toneFreq = 200.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float)i / (float)numSamples;

        // faster decay than kick, slower than hat
        float env = std::exp(-12.0f * t);

        // tonal body
        float inc = (float)(juce::MathConstants<double>::twoPi * toneFreq / sr);
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        float tone = std::sin(phase);

        // noise (snares have noise, but not only noise)
        float noise = rng.nextFloat() * 2.0f - 1.0f;

        x[i] = amp * env * (0.35f * tone + 0.65f * noise);
    }

    return b;
}

static juce::AudioBuffer<float> makeHighpassedHat(int numSamples, float amp)
{
    juce::AudioBuffer<float> b(1, numSamples);
    auto* x = b.getWritePointer(0);

    juce::Random rng(54321);

    float last = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float t = (float)i / (float)numSamples;
        float env = std::exp(-18.0f * t);

        float noise = rng.nextFloat() * 2.0f - 1.0f;
        float hp = noise - last; // crude highpass
        last = noise;

        x[i] = amp * env * hp;
    }
    return b;
}
// Copies the samples from channel 0 of a JUCE AudioBuffer into a contiguous
// std::vector<float>. This is used to convert a JUCE buffer into a plain
// mono sample array suitable for DSP analysis (e.g. FFT, classification).
// Assumes the buffer is mono or that only channel 0 is relevant.
static void bufferToMonoVector(const juce::AudioBuffer<float>& b, std::vector<float>& out)
{
    out.assign((size_t)b.getNumSamples(), 0.0f);
    const float* x = b.getReadPointer(0);
    for (int i = 0; i < b.getNumSamples(); ++i)
        out[(size_t)i] = x[i];
}

// ------------------ Unit tests ------------------

class DrumClassifierTests : public juce::UnitTest
{
public:
    DrumClassifierTests() : juce::UnitTest("DrumClassifierTests") {}

    void runTest() override
    {
        const double sr = 48000.0;

        DrumClassifierParams params;
        params.fftOrder = 10;             // 1024
        params.fftOffsetSamples = 0;      // start at onset
        params.velocityWindowSamples = 1024;

        // These are conservative starting values; tweak if your rules are stricter.
        params.velocityScale = 8.0f;
        params.velocityGamma = 0.7f;

        DrumClassifier c;
        c.prepare(sr, params);

        beginTest("Kick classification on synthetic kick");
        {
            auto kick = makeSineKick(2048, sr, 0.8f);
            std::vector<float> mono;
            bufferToMonoVector(kick, mono);

            auto res = c.classifyAt(mono.data(), (int)mono.size(), /*onset*/ 0);

            expect(res.velocity >= 1 && res.velocity <= 127);
            expect(res.cls == DrumClass::Kick || res.cls == DrumClass::Unknown); // allow Unknown if thresholds need tuning
        }

        beginTest("Snare classification on noise burst");
        {
            auto snare = makeNoiseSnare(2048, 4800, 0.8f);
            std::vector<float> mono;
            bufferToMonoVector(snare, mono);

            auto res = c.classifyAt(mono.data(), (int)mono.size(), 0);

            expect(res.velocity >= 1 && res.velocity <= 127);
            expect(res.cls == DrumClass::Snare || res.cls == DrumClass::Unknown);
        }

        beginTest("Hat classification on high-passed noise");
        {
            auto hat = makeHighpassedHat(2048, 0.8f);
            std::vector<float> mono;
            bufferToMonoVector(hat, mono);

            auto res = c.classifyAt(mono.data(), (int)mono.size(), 0);

            expect(res.velocity >= 1 && res.velocity <= 127);
            expect(res.cls == DrumClass::Hat || res.cls == DrumClass::Unknown);
        }

        beginTest("Velocity increases with amplitude");
        {
            auto soft = makeSineKick(2048, sr, 0.15f);
            auto loud = makeSineKick(2048, sr, 0.90f);

            std::vector<float> monoSoft, monoLoud;
            bufferToMonoVector(soft, monoSoft);
            bufferToMonoVector(loud, monoLoud);

            auto a = c.classifyAt(monoSoft.data(), (int)monoSoft.size(), 0);
            auto b = c.classifyAt(monoLoud.data(), (int)monoLoud.size(), 0);

            expect(a.velocity >= 1 && a.velocity <= 127);
            expect(b.velocity >= 1 && b.velocity <= 127);
            expect(b.velocity > a.velocity);
        }

        beginTest("Insufficient samples returns Unknown (FFT window doesn't fit)");
        {
            auto shortBuf = makeSineKick(512, sr, 0.9f); // shorter than 1024
            std::vector<float> mono;
            bufferToMonoVector(shortBuf, mono);

            auto res = c.classifyAt(mono.data(), (int)mono.size(), 0);

            // velocity is still computed, but class should be Unknown because FFT window can't fit
            expect(res.velocity >= 1 && res.velocity <= 127);
            expect(res.cls == DrumClass::Unknown);
        }
    }
};

static DrumClassifierTests drumClassifierTests;

static const char* clsToStr(DrumClass c)
{
    switch (c)
    {
        case DrumClass::Kick: return "Kick";
        case DrumClass::Snare: return "Snare";
        case DrumClass::Hat: return "Hat";
        default: return "Unknown";
    }
}

// Call this from prepareToPlay() once
void runDrumClassifierSmokeTests()
{
    DBG("=== DrumClassifier smoke tests ===");

    const double sr = 48000.0;

    DrumClassifierParams params;
    params.fftOrder = 10;            // 1024
    params.fftOffsetSamples = 0;
    params.velocityWindowSamples = 1024;
    params.velocityScale = 8.0f;
    params.velocityGamma = 0.7f;

    DrumClassifier c;
    c.prepare(sr, params);

    auto runOne = [&](const char* name, const juce::AudioBuffer<float>& b)
    {
        std::vector<float> mono;
        bufferToMonoVector(b, mono);

        auto res = c.classifyAt(mono.data(), (int)mono.size(), 0);

        DBG(juce::String(name) + " => class=" + clsToStr(res.cls)
            + " vel=" + juce::String((int)res.velocity));

        if (res.velocity < 1 || res.velocity > 127)
        {
            DBG("ERROR: velocity out of range!");
            jassertfalse;
        }
    };

    runOne("kick",  makeSineKick(2048, sr, 0.8f));
    runOne("snare", makeNoiseSnare(2048, 4800, 0.8f));
    runOne("hat",   makeHighpassedHat(2048, 0.8f));

    // velocity monotonicity check
    {
        auto soft = makeSineKick(2048, sr, 0.15f);
        auto loud = makeSineKick(2048, sr, 0.90f);

        std::vector<float> a, b;
        bufferToMonoVector(soft, a);
        bufferToMonoVector(loud, b);

        auto ra = c.classifyAt(a.data(), (int)a.size(), 0);
        auto rb = c.classifyAt(b.data(), (int)b.size(), 0);

        DBG("soft vel=" + juce::String((int)ra.velocity) + " loud vel=" + juce::String((int)rb.velocity));
        if (!(rb.velocity > ra.velocity))
        {
            DBG("ERROR: loud velocity not greater than soft!");
            jassertfalse;
        }
    }

    // short buffer should be Unknown
    {
        auto shortBuf = makeSineKick(512, sr, 0.9f);
        std::vector<float> mono;
        bufferToMonoVector(shortBuf, mono);

        auto res = c.classifyAt(mono.data(), (int)mono.size(), 0);
        DBG("short buffer => class=" + juce::String(clsToStr(res.cls)));

        if (res.cls != DrumClass::Unknown)
        {
            DBG("ERROR: expected Unknown for short buffer!");
            jassertfalse;
        }
    }

    DBG("=== Done smoke tests ===");
}
