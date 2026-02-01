#include "DrumClassifier.h"
#include <cmath>
#include <algorithm>
#include "InputProcessor.h"

void DrumClassifier::prepare(double sampleRate, const DrumClassifierParams& params)
{
    sr = sampleRate;
    p = params;

    fftSize = 1 << p.fftOrder;

    fft = std::make_unique<juce::dsp::FFT>(p.fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(
        (size_t)fftSize,
        juce::dsp::WindowingFunction<float>::hann
    );

    fftData.assign((size_t)fftSize * 2, 0.0f);
    mags.assign((size_t)fftSize / 2, 0.0f);
}


void DrumClassifier::reset()
{
    // no persistent state in this classifier
}

ClassifiedHit DrumClassifier::classifyAt(const float* monoSamples, int numSamples, int64_t onsetSample) const
{
    ClassifiedHit out;
    out.onsetSample = onsetSample;

    if (monoSamples == nullptr || numSamples <= 0)
        return out;

    // ---- Velocity: peak absolute amplitude over a window after onset ----
    {
        const int start = (int)juce::jlimit<int64_t>(0, numSamples - 1, onsetSample);
        const int end   = (int)juce::jlimit<int64_t>(0, numSamples, onsetSample + p.velocityWindowSamples);

        float peak = 0.0f;
        for (int i = start; i < end; ++i)
            peak = juce::jmax(peak, std::abs(monoSamples[i]));

        out.velocity = peakToMidiVelocity(peak);
    }

    // ---- FFT slice for classification ----
    const int fftStart = (int)onsetSample + p.fftOffsetSamples;

    if (fftStart < 0 || fftStart + fftSize > numSamples)
    {
        out.cls = DrumClass::Unknown;
        return out;
    }

    // pack samples into fftData
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t)i] = monoSamples[fftStart + i];

    // window + FFT
    window->multiplyWithWindowingTable(fftData.data(), (size_t)fftSize);
    fft->performFrequencyOnlyForwardTransform(fftData.data());


    for (int k = 0; k < fftSize / 2; ++k)
        mags[(size_t)k] = fftData[(size_t)k];

    // compute band energies (power) + centroid
    const float low  = bandEnergy(p.lowLo,  p.lowHi);
    const float mid  = bandEnergy(p.midLo,  p.midHi);
    const float high = bandEnergy(p.highLo, p.highHi);
    const float total = low + mid + high + 1e-9f;

    const float lowR  = low / total;
    const float midR  = mid / total;
    const float highR = high / total;
    const float c = centroidHz();

    // rule-based classification
    if (lowR > p.kickLowRatio && c < p.kickCentroidHz)
        out.cls = DrumClass::Kick;
    else if (midR > p.snareMidRatio && c > p.snareCentroidHz)
        out.cls = DrumClass::Snare;
    else if (highR > p.hatHighRatio &&
             c > p.hatCentroidHz &&
             high > (p.hatHighOverMid * mid))
    {
        out.cls = DrumClass::Hat;
    }
    else
        out.cls = DrumClass::Unknown;

    return out;
}

juce::uint8 DrumClassifier::peakToMidiVelocity(float peakAbs) const noexcept
{
    // scale into 0..1
    float v01 = peakAbs * p.velocityScale;
    v01 = juce::jlimit(0.0f, 1.0f, v01);

    // curve for feel
    v01 = std::pow(v01, p.velocityGamma);

    int v = (int)std::lround(v01 * 126.0f) + 1; // 1..127
    v = juce::jlimit(1, 127, v);
    return (juce::uint8)v;
}

int DrumClassifier::hzToBin(float hz) const noexcept
{
    float bin = hz * (float)fftSize / (float)sr;
    return (int)juce::jlimit(0.0f, (float)(fftSize/2 - 1), bin);
}

float DrumClassifier::bandEnergy(float hzLo, float hzHi) const noexcept
{
    int lo = hzToBin(hzLo);
    int hi = hzToBin(hzHi);
    if (hi <= lo) return 0.0f;

    float sum = 0.0f;
    for (int k = lo; k <= hi; ++k)
    {
        float m = mags[(size_t)k];
        sum += m * m;
    }
    return sum;
}

float DrumClassifier::centroidHz() const noexcept
{
    float num = 0.0f, den = 0.0f;
    for (int k = 1; k < fftSize/2; ++k)
    {
        float f = (float)k * (float)sr / (float)fftSize;
        float w = mags[(size_t)k];
        num += f * w;
        den += w;
    }
    return (den > 1e-9f) ? (num / den) : 0.0f;
}

static void mixToMono(const juce::AudioBuffer<float>& in, std::vector<float>& outMono)
{
    const int n = in.getNumSamples();
    const int chs = in.getNumChannels();

    outMono.assign((size_t)n, 0.0f);
    if (chs <= 0) return;

    if (chs == 1)
    {
        const float* x = in.getReadPointer(0);
        for (int i = 0; i < n; ++i)
            outMono[(size_t)i] = x[i];
        return;
    }

    for (int ch = 0; ch < chs; ++ch)
    {
        const float* x = in.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
            outMono[(size_t)i] += x[i];
    }

    const float inv = 1.0f / (float)chs;
    for (int i = 0; i < n; ++i)
        outMono[(size_t)i] *= inv;
}

ClassifiedMouthHit DrumClassifier::classifyMouthHit(const MouthHit& hit, int onsetInHitBuffer) const
{
    ClassifiedMouthHit out;
    out.onsetSample = hit.onsetSample;

    const auto& buf = hit.buffer;
    const int n = buf.getNumSamples();
    if (n <= 0)
        return out;

    // Convert this hit's buffer to contiguous mono samples
    std::vector<float> mono;
    mixToMono(buf, mono);

    // classify within this hit buffer (onset is usually 0 unless you have pre-roll)
    auto res = classifyAt(mono.data(), (int)mono.size(), (int64_t)onsetInHitBuffer);

    out.velocity = res.velocity;
    out.cls = res.cls;
    return out;
}
