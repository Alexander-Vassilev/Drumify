#include "hitClassifier.h"
#include "utils.h"
#include <algorithm>

PooledHitFeatures HitClassifier::totalFeatures {};
HitClassifier::FeatureStats HitClassifier::batchStats {};

const char* const HitClassifier::trackedFeatureNames[HitClassifier::numTrackedFeatures]
{
    "MeanCentroid", "Delta", "TopEndHeavyRatio", "LowEndHeavyRatio", "HighLowDecayRatio",
    "EnergyWeight", "TransientZCR", "CentroidNoBass", "HighpassZCR"
};

float HitClassifier::computeRMS(const juce::AudioBuffer<float>& buffer, int length)
{
    if (length <= 0 || buffer.getNumSamples() <= 0)
        return 0.0f;

    length = juce::jmin(length, buffer.getNumSamples());

    const float* data = buffer.getReadPointer(0);
    double sumSq = 0.0;

    for (int i = 0; i < length; ++i)
        sumSq += (double)data[i] * (double)data[i];

    return (float)std::sqrt(sumSq / (double)length);
}

float HitClassifier::computeZeroCrossingRate(const juce::AudioBuffer<float>& buffer, int length)
{
    if (length <= 1 || buffer.getNumSamples() <= 1)
        return 0.0f;

    length = juce::jmin(length, buffer.getNumSamples());

    const float* data = buffer.getReadPointer(0);
    int crossings = 0;

    for (int i = 1; i < length; ++i)
    {
        const float a = data[i - 1];
        const float b = data[i];

        if ((a >= 0.0f && b < 0.0f) || (a < 0.0f && b >= 0.0f))
            crossings++;
    }

    return (float)crossings / (float)length;
}

float HitClassifier::computeZeroCrossingRateInRange(const juce::AudioBuffer<float>& buffer,
                                                    int startSample, int endSample)
{
    endSample = juce::jmin(endSample, buffer.getNumSamples());
    startSample = juce::jmax(0, startSample);

    const int span = endSample - startSample;

    if (span <= 1)
        return 0.0f;

    const float* data = buffer.getReadPointer(0);
    int crossings = 0;

    for (int i = startSample + 1; i < endSample; ++i)
    {
        const float a = data[i - 1];
        const float b = data[i];

        if ((a >= 0.0f && b < 0.0f) || (a < 0.0f && b >= 0.0f))
            crossings++;
    }

    // Normalised by the window actually measured, so a truncated window at the
    // end of a short hit stays comparable with a full one.
    return (float)crossings / (float)span;
}

float HitClassifier::computeSchmittZeroCrossingRateInRange(const juce::AudioBuffer<float>& buffer,
                                                           int startSample, int endSample)
{
    endSample = juce::jmin(endSample, buffer.getNumSamples());
    startSample = juce::jmax(0, startSample);

    const int span = endSample - startSample;

    if (span <= 1)
        return 0.0f;

    const float* data = buffer.getReadPointer(0);

    // Absolute magnitude threshold (hysteresis deadband).
    // Samples must clear this level on the opposite side to trigger a state flip.
    // Adjust this value based on expected background noise floor (e.g., -40 dB ~= 0.01f).
    constexpr float hysteresisThreshold = 0.02f;

    // Track the current polarity state (+1 or -1)
    int currentPolarity = (data[startSample] >= 0.0f) ? 1 : -1;
    int crossings = 0;

    for (int i = startSample + 1; i < endSample; ++i)
    {
        const float sample = data[i];
        //DBG("sample: " << sample);

        if (currentPolarity == 1)
        {
            // Currently in POSITIVE state: wait for sample to drop below the negative threshold
            if (sample < -hysteresisThreshold)
            {
                ++crossings;
                currentPolarity = -1;
            }
        }
        else
        {
            // Currently in NEGATIVE state: wait for sample to exceed the positive threshold
            if (sample > hysteresisThreshold)
            {
                ++crossings;
                currentPolarity = 1;
            }
        }
    }

    return static_cast<float>(crossings) / static_cast<float>(span);
}

/** Windowed-sinc FIR highpass. Linear phase by construction (the kernel is
    symmetric), so every frequency is delayed by exactly the same amount - the
    centre tap - and that delay is taken back out at the point of use, leaving
    the transient where findTransientStart put it.

    Built as a lowpass and spectrally inverted (negate, then add one to the
    centre tap), which turns a windowed-sinc lowpass into the complementary
    highpass. Blackman gives a transition of about 5.5/N of the sample rate:
    118 Hz at 2047 taps and 44.1 kHz, with the stopband down over 70 dB.
*/
static const std::vector<float>& getHighpassZcrKernel(double sampleRate)
{
    static std::vector<float> kernel;
    static double kernelSampleRate = 0.0;

    if (kernelSampleRate == sampleRate && ! kernel.empty())
        return kernel;

    constexpr int taps = highpassZcrFirTaps;
    constexpr int centre = taps / 2;

    kernel.assign(taps, 0.0f);

    const double fc = (double)highpassZcrCutoffHz / sampleRate;   // cycles per sample
    double dcGain = 0.0;

    for (int n = 0; n < taps; ++n)
    {
        const double k = (double)(n - centre);
        const double sinc = k == 0.0 ? 2.0 * fc
                                     : std::sin(2.0 * juce::MathConstants<double>::pi * fc * k)
                                         / (juce::MathConstants<double>::pi * k);

        const double phase = 2.0 * juce::MathConstants<double>::pi * (double)n / (double)(taps - 1);
        const double blackman = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);

        kernel[n] = (float)(sinc * blackman);
        dcGain += kernel[n];
    }

    // Unity DC gain on the lowpass, so the inverted highpass has exactly zero.
    for (auto& tap : kernel)
        tap = (float)(-tap / dcGain);

    kernel[centre] += 1.0f;

    kernelSampleRate = sampleRate;
    return kernel;
}

float HitClassifier::computeHighpassedTransientZcr(const juce::AudioBuffer<float>& buffer, int length,
                                                    int transientStart, double sampleRate)
{
    length = juce::jmin(length, buffer.getNumSamples());

    const int windowStart = juce::jmax(0, transientStart);
    const int windowEnd = juce::jmin(length, transientStart + transientZcrWindowLength);
    const int windowLength = windowEnd - windowStart;

    if (windowLength <= 1)
        return 0.0f;

    const auto& kernel = getHighpassZcrKernel(sampleRate > 0.0 ? sampleRate : 44100.0);
    const int taps = (int)kernel.size();
    const int centre = taps / 2;

    const float* input = buffer.getReadPointer(0);

    // Only the window is needed, so only the window is filtered: each output
    // sample is the kernel centred on the matching input sample, which is the
    // delay compensation. Reads past either end of the hit see silence.
    juce::AudioBuffer<float> filtered(1, windowLength);
    float* output = filtered.getWritePointer(0);

    for (int k = 0; k < windowLength; ++k)
    {
        const int inputCentre = windowStart + k;
        double acc = 0.0;

        for (int j = 0; j < taps; ++j)
        {
            const int i = inputCentre + (j - centre);

            if (i >= 0 && i < length)
                acc += (double)kernel[j] * (double)input[i];
        }

        output[k] = (float)acc;
    }

    // Plain sign-change counting rather than the Schmitt version: the highpass
    // has already stripped the sub that the hysteresis was guarding against,
    // and its fixed 0.02 threshold would sit above much of what is left.
    return computeZeroCrossingRateInRange(filtered, 0, windowLength);
}

int HitClassifier::findTransientStart(const juce::AudioBuffer<float>& buffer, int length, double sampleRate)
{
    length = juce::jmin(length, buffer.getNumSamples());

    if (length <= 1)
        return transientZcrFallbackStart;

    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Fast attack to catch the strike, slower release so the envelope does not
    // collapse between samples of a waveform's own cycle.
    const float attackCoeff  = (float)std::exp(-1.0 / (0.001 * rate));
    const float releaseCoeff = (float)std::exp(-1.0 / (0.020 * rate));

    const float* data = buffer.getReadPointer(0);

    const auto stepEnvelope = [&](float env, float rectified)
    {
        const float coeff = rectified > env ? attackCoeff : releaseCoeff;
        return coeff * (env - rectified) + rectified;
    };

    // Pass 1: the hit's peak envelope, which sets the threshold.
    float env = 0.0f;
    float peakEnv = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        env = stepEnvelope(env, std::abs(data[i]));
        peakEnv = juce::jmax(peakEnv, env);
    }

    if (peakEnv <= 1.0e-6f)
        return transientZcrFallbackStart;

    // A hit that is already loud where it begins was separated spectrally rather
    // than by amplitude, so it has no spike to find. The envelope alone cannot
    // tell us this - it starts from zero and so always appears to rise - hence
    // comparing the raw level at the head of the hit against its overall peak.
    const int headLength = juce::jmin(length, 128);
    float headPeak = 0.0f;

    for (int i = 0; i < headLength; ++i)
        headPeak = juce::jmax(headPeak, std::abs(data[i]));

    if (headPeak >= peakEnv * 0.5f)
        return transientZcrFallbackStart;

    // Pass 2: the first crossing of half the peak. Recomputed rather than stored
    // so this stays allocation-free per hit.
    const float threshold = peakEnv * 0.5f;

    env = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        env = stepEnvelope(env, std::abs(data[i]));

        if (env >= threshold)
        {
            // Already above threshold at the very first sample means the hit was
            // carved out of sustained material and never actually rose - there is
            // no transient here to anchor the window to.
            return i > 0 ? i : transientZcrFallbackStart;
        }
    }

    return transientZcrFallbackStart;
}

/*std::vector<std::vector<juce::dsp::Complex<float>>> HitClassifier::getSTFT(const juce::AudioBuffer<float>& buffer, int length) {
    for (int i = 0; i < length - fftSize; i += hopSize) {
        
    }
}*/

HitFeatures HitClassifier::extractFeatures(const juce::AudioBuffer<float>& buffer, int length, double sampleRate)
{
    HitFeatures f;
    f.rms = computeRMS(buffer, length);
    f.zcr = computeZeroCrossingRate(buffer, length);
    f.durationSec = (sampleRate > 0.0) ? (float)length / (float)sampleRate : 0.0f;

    // ZCR over a fixed window anchored at the transient, so every hit is measured
    // over the same stretch of its attack regardless of how much silence the
    // onset detector left in front of it. Truncated by the range helper when the
    // hit ends before the window does.
    const int transientStart = findTransientStart(buffer, length, sampleRate);
    DBG("ZCR calc starts: " << transientStart);
    f.transientZcr = computeSchmittZeroCrossingRateInRange(buffer, transientStart,
                                                           transientStart + transientZcrWindowLength);

    // The same measurement with everything below 150 Hz removed first, so a
    // kick's fundamental cannot hold the crossing rate down on its own.
    f.highpassZcr = computeHighpassedTransientZcr(buffer, length, transientStart, sampleRate);
    
    DBG("length: " << f.durationSec);
    
    if (length < 1025) {
        f.fftActive = false;
    } else {
        auto* inputData = buffer.getReadPointer(0);
        int numSamples = juce::jmin(length, buffer.getNumSamples());

        // Scale the hit up to 0 dBFS for the FFT only, so a quiet sample and a
        // loud one of the same drum yield the same spectrum. Applied on the way
        // into the FFT rather than to the buffer, which is const and shared.
        // f.rms above deliberately keeps the original level - it drives velocity.
        const float peak = buffer.getMagnitude(0, 0, numSamples);
        const float normalisingGain = peak > 0.0f ? 1.0f / peak : 1.0f;

        fft.reset();

        for (int i = 0; i < numSamples; i++) {
            std::optional<std::array<float, FFTProcessor::numBins>> retVal
                = fft.processSample(inputData[i] * normalisingGain);

            if (retVal.has_value()) {
                f.stftData.push_back(*retVal);
            }
        }
    }
    
    return f;
}

float HitClassifier::getDelta(const std::vector<float>& values, const std::vector<float>& volumes, const std::vector<float>& avgEnergies,
                              float* energyWeightOut)
{
    const int intendedFrameCount = 6;

    // Measured from a fixed frame rather than wherever the loudest frame landed:
    // the attack is over by frame 3, so this compares the same part of every hit
    // regardless of how the peak amplitude fell.
    const int startFrame = 1;
    static constexpr float scalingFactor = 10.0f;

    if (energyWeightOut != nullptr)
        *energyWeightOut = 0.0f;

    // 1. Guard: Ensure vectors are valid, match in size, and have at least 6 frames
    const int numFrames = static_cast<int>(values.size());
    if (numFrames < intendedFrameCount
         || volumes.size() != values.size()
         || avgEnergies.size() != values.size())
        return 0.0f;

    // 2. Clamp so a hit too short to reach frame 3 still yields 6 frames.
    const int startIndex = std::clamp(startFrame, 0, numFrames - intendedFrameCount);
    DBG("start index for delta finding: " << startIndex);

    // Constant parameters for exactly 6 iterations
    const float M = static_cast<float>(intendedFrameCount);
    const int loopEnd = startIndex + intendedFrameCount;

    float sumX  = 0.0f;
    float sumY  = 0.0f;
    float sumXY = 0.0f;
    float sumXX = 0.0f;
    float sumEnergies = 0.0f;

    // 4. Loop runs exactly 6 times starting at startIndex
    for (int i = startIndex; i < loopEnd; ++i)
    {
        float x = static_cast<float>(i - startIndex); // x goes from 0.0 to 5.0
        float y = values[i];
        sumEnergies += avgEnergies[i];

        DBG("value for delta: " << y << " at adjusted index x: " << x << " (original index: " << i << ")");
        
        sumX  += x;
        sumY  += y;
        sumXY += x * y;
        sumXX += x * x;
    }

    int numIter = loopEnd - startIndex;
    sumEnergies /= static_cast<float>(numIter);
    // --- Mathematical DSP Insight ---
    // Because M is fixed at 6, and x is always [0, 1, 2, 3, 4, 5]:
    // - sumX is always 15.0f
    // - sumXX is always 55.0f
    // - denominator is always: (6 * 55) - (15 * 15) = 330 - 225 = 105.0f
    float denominator = (M * sumXX) - (sumX * sumX);
    
    if (std::abs(denominator) > 1e-5f)
    {
        float result = ((M * sumXY) - (sumX * sumY)) / denominator;
        float energyWeight = 2 * (std::log(sumEnergies) - 2.9);
        DBG("Start index: " << startIndex << " | 6-frame delta: " << result);
        DBG("Delta weight from total energy: " << energyWeight);

        if (energyWeightOut != nullptr)
            *energyWeightOut = energyWeight;

        return scalingFactor * result;
        //return scalingFactor * result * energyWeight;
    }
    
    return 0.0f;
}

double HitClassifier::calculateGaussianPDF(double x, const FeatureDistribution& dist)
{
    // Avoid division by zero
    double stdev = dist.stdev;

    if (stdev <= 0.0)
        stdev = 1e-5;

    double z = (x - dist.mean) / stdev;

    // One-sided: for monotonic evidence, everything past the mean is at least as
    // characteristic of the class, so the falloff applies on one side only. This
    // flattens the curve above the mean rather than shifting it.
    if (dist.direction == FeatureDirection::higherIsBetter)
        z = std::min(z, 0.0);
    else if (dist.direction == FeatureDirection::lowerIsBetter)
        z = std::max(z, 0.0);

    // Deliberately not a true density: the 1/(sigma*sqrt(2pi)) coefficient is
    // dropped so every feature peaks at exactly 1.0 at its mean. With it, a
    // narrow feature (sigma 0.03) peaked at 13 and a wide one (sigma 425) at
    // 0.0009, so the product was dominated by how spread out each feature was
    // rather than by how well the hit matched - and clamping the narrow ones to
    // 1.0 then flattened their whole central region into an uninformative
    // plateau. On a common [0, 1] scale, the class weights mean what they say.
    return std::exp(-(z * z) / 2.0);
}

double HitClassifier::calculateClassLikelihood(const PooledHitFeatures& f, const DrumClassParameters& params)
{
    double pCentroid = calculateGaussianPDF(f.meanCentroid,       params.meanCentroid);
    double pNoBass   = calculateGaussianPDF(f.meanCentroidNoBass, params.centroidNoBass);
    double pDelta    = calculateGaussianPDF(f.centroidDelta,      params.delta);
    double pTop      = calculateGaussianPDF(f.topEndHeavyRatio, params.topEndHeavy);
    double pLow      = calculateGaussianPDF(f.lowEndHeavyRatio, params.lowEndHeavy);
    double pDecay    = calculateGaussianPDF(f.decayRatio,       params.decayRatio);
    double pZcr      = calculateGaussianPDF(f.transientZcr,     params.transientZcr);
    double pHpZcr    = calculateGaussianPDF(f.highpassZcr,      params.highpassZcr);

    // No clamping needed: calculateGaussianPDF already tops out at 1.0.

    DBG (juce::String::formatted (
        "pCentroid: %-6.2f | pCentroidNoBass: %-6.2f | pDelta: %-6.2f | pTopEndHeavyRatio: %-6.2f | pLowEndHeavyRatio: %-6.2f | pHighLowDecayRatio: %-6.2f | pTransientZCR: %-6.2f | pHighpassZCR: %-6.2f",
        pCentroid, pNoBass, pDelta, pTop, pLow, pDecay, pZcr, pHpZcr
    ));

    double wCentroid = std::pow(pCentroid, params.weights.centroidWeight);
    double wNoBass   = std::pow(pNoBass,   params.weights.centroidNoBassWeight);
    double wDelta    = std::pow(pDelta,    params.weights.deltaWeight);
    double wTop      = std::pow(pTop,      params.weights.topWeight);
    double wLow      = std::pow(pLow,      params.weights.lowWeight);
    double wDecay    = std::pow(pDecay,    params.weights.decayWeight);
    double wZcr      = std::pow(pZcr,      params.weights.zcrWeight);
    double wHpZcr    = std::pow(pHpZcr,    params.weights.highpassZcrWeight);

    // Naive Bayes Assumption: Multiply the independent feature probabilities together
    return wCentroid * wNoBass * wDelta * wTop * wLow * wDecay * wZcr * wHpZcr;
}

// Heuristic classification:
// - Hat: high ZCR + short duration
// - Kick: low ZCR + longer duration + decent RMS
// - Snare: mid ZCR band
HitType HitClassifier::classify(const HitFeatures& f, std::ofstream& csvFile)
{
    DBG("fftActive: " << static_cast<int>(f.fftActive));
    if (f.fftActive) {
        const int movAvgSize = 3;
        const int numWindows = f.stftData.size();
        int analysisLen = std::min(numWindows, 28);
        std::vector<float> spectralCentroids;
        std::vector<float> noBassCentroids;
        std::vector<float> lowMidCentroids;
        std::vector<float> avgLowEnergies;
        std::vector<float> avgLowMidEnergies;
        std::vector<float> avgMidEnergies;
        std::vector<float> avgMidHighEnergies;
        std::vector<float> avgHighEnergies;
        std::vector<float> prominences;
        std::vector<float> totalEnergies;
        std::vector<int> dominantBands;
        
        for (int i = 0; i < analysisLen; i++) {
            std::array<float, numFilters> fftFilterbank = applyMelFilterbank(f.stftData[i], 44100);
            
            float totalEnergy = 0.0f;
            
            for (int j = 0; j < numFilters; j++) totalEnergy += fftFilterbank[j];
            
            const float silenceThreshold = 10.0f;
            
            if (totalEnergy < silenceThreshold) continue;
            
            for (int j = 0; j < numFilters; j++)
                std::cout << std::fixed << std::setprecision(1) << std::setw(5) << fftFilterbank[j] << " ";
            std::cout << std::endl;
            
            // 2. Extract features cleanly using the helper class
            float centroid = DrumFeatureExtractor::calculateSpectralCentroid(fftFilterbank, 0, numFilters);

            // Same centroid with the two lowest mel bands left out, so the sub
            // and bass that anchor a kick's centroid do not dominate. What is
            // left is the brightness of everything above the fundamental -
            // the beater click on a kick, the wires on a snare.
            float noBassCentroid = DrumFeatureExtractor::calculateSpectralCentroid(fftFilterbank,
                                                                                    noBassCentroidFirstBand,
                                                                                    numFilters);
            float lowMidCentroid = DrumFeatureExtractor::calculateSpectralCentroid(fftFilterbank, 0, 8);
            float prominence = DrumFeatureExtractor::calculateLowMidProminence(f.stftData[i], 44100, FFTProcessor::numBins);
            float lowEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 0, 3);
            float lowMidEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 0, 5);
            float midEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 5, 12);
            float midHighEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 14, 23);
            float highEnergy = DrumFeatureExtractor::calculateAvgEnergyInBand(fftFilterbank, 17, 25);
            
            spectralCentroids.push_back(centroid);
            noBassCentroids.push_back(noBassCentroid);
            lowMidCentroids.push_back(lowMidCentroid);
            prominences.push_back(prominence);
            avgLowEnergies.push_back(lowEnergy);
            avgLowMidEnergies.push_back(lowMidEnergy);
            avgMidEnergies.push_back(midEnergy);
            avgMidHighEnergies.push_back(midHighEnergy);
            avgHighEnergies.push_back(highEnergy);
            totalEnergies.push_back(totalEnergy);
        }
        
        analysisLen = spectralCentroids.size();
        
        PooledHitFeatures pooledFeatures;
        
        // 3. Pool values over time
        float centroidSum = 0.0f;
        float noBassCentroidSum = 0.0f;
        float prominenceSum = 0.0f;

        // Mean of the per-frame band ratios rather than a count of frames that
        // merely exceed mid: high sitting at twice mid on every frame now reads
        // as 2.0, so the magnitude of the imbalance survives, not just its sign.
        float topEndRatioSum = 0.0f;
        float lowEndRatioSum = 0.0f;
        int topEndRatioFrames = 0;
        int lowEndRatioFrames = 0;

        // Frames whose mid band is silent would divide by ~0 and swamp the mean,
        // so they are left out of the average entirely.
        constexpr float minMidEnergy = 1.0e-6f;

        const int numFramesConsiderTopEndHeavy = std::min(9, analysisLen);
        const int numFramesConsiderLowEndHeavy = std::min(30, analysisLen);

        for (int i = 0; i < analysisLen; ++i) {
            centroidSum += spectralCentroids[i];
            noBassCentroidSum += noBassCentroids[i];
            prominenceSum += prominences[i];
            //DBG("Low avg nrg: " << avgLowEnergies[i]);
            //DBG("Mid avg nrg: " << avgMidEnergies[i]);
            //DBG("High avg nrg: " << avgHighEnergies[i]);

            if (avgMidEnergies[i] <= minMidEnergy)
                continue;

            if (i < numFramesConsiderTopEndHeavy)
            {
                topEndRatioSum += avgHighEnergies[i] / avgMidEnergies[i];
                ++topEndRatioFrames;
            }

            if (i < numFramesConsiderLowEndHeavy)
            {
                lowEndRatioSum += avgLowEnergies[i] / avgMidEnergies[i];
                ++lowEndRatioFrames;
            }
        }

        pooledFeatures.meanCentroid = centroidSum / static_cast<float>(analysisLen);
        pooledFeatures.meanCentroidNoBass = noBassCentroidSum / static_cast<float>(analysisLen);
        pooledFeatures.meanLowMidProminence = prominenceSum / static_cast<float>(analysisLen);
        pooledFeatures.topEndHeavyRatio = topEndRatioFrames > 0 ? topEndRatioSum / static_cast<float>(topEndRatioFrames) : 0.0f;
        pooledFeatures.lowEndHeavyRatio = lowEndRatioFrames > 0 ? lowEndRatioSum / static_cast<float>(lowEndRatioFrames) : 0.0f;
        
        if (!spectralCentroids.empty())
        {
            // A. Calculate Mean (Overall Brightness)
            float sum = 0.0f;
            for (float c : spectralCentroids) {
                sum += c;
            }
            pooledFeatures.meanCentroid = sum / static_cast<float>(spectralCentroids.size());

            // B. Calculate Standard Deviation (Spectral Stability over time)
            float varianceSum = 0.0f;
            for (float c : spectralCentroids) {
                float diff = c - pooledFeatures.meanCentroid;
                varianceSum += diff * diff;
            }
            pooledFeatures.centroidStdDev = std::sqrt(varianceSum / static_cast<float>(spectralCentroids.size()));

            // C. Calculate Delta (Spectral Shift Direction: End - Start)
            //pooledFeatures.centroidDelta = getDelta(spectralCentroids);
            pooledFeatures.centroidDelta = getDelta(lowMidCentroids, totalEnergies, avgLowMidEnergies,
                                                    &pooledFeatures.deltaEnergyWeight);
            
            pooledFeatures.lowDecayCentroid  = DrumFeatureExtractor::calculateTemporalCentroid(avgLowEnergies);
            pooledFeatures.highDecayCentroid = DrumFeatureExtractor::calculateTemporalCentroid(avgMidHighEnergies);
            
            pooledFeatures.decayRatio = pooledFeatures.lowDecayCentroid / (pooledFeatures.highDecayCentroid + 1e-5f);

            // Time-domain, so it is carried straight across from extractFeatures
            // rather than pooled over the STFT frames.
            pooledFeatures.transientZcr = f.transientZcr;
            pooledFeatures.highpassZcr = f.highpassZcr;
            
            DBG (juce::String::formatted (
                "Decay -> Lows: %-5.2f | Highs: %-5.2f | Ratio (L/H): %-5.2f",
                pooledFeatures.lowDecayCentroid,
                pooledFeatures.highDecayCentroid,
                pooledFeatures.decayRatio
            ));
        }
        
        // Print the resulting dynamic footprint
        DBG (juce::String::formatted (
            "Mean Centroid: %-6.2f | Mean No-Sub Centroid: %-6.2f | Delta: %-6.2f | TopEndHeavyCount: %-6.2f | LowEndHeavyCount: %-6.2f | HighLowDecayRatio: %-6.2f | ZCR: %-6.2f",
            pooledFeatures.meanCentroid,
            pooledFeatures.meanCentroidNoBass,
            pooledFeatures.centroidDelta,
            pooledFeatures.topEndHeavyRatio,
            pooledFeatures.lowEndHeavyRatio,
            pooledFeatures.decayRatio,
            pooledFeatures.transientZcr
        ));
        
        increaseFeatureCount(pooledFeatures);
        
        if (csvFile.is_open())
        {
            // Write each column separated by a comma, ending with std::endl to flush to disk
            csvFile << std::fixed << std::setprecision(4) << ","
                    << pooledFeatures.meanCentroid << ","
                    << pooledFeatures.centroidDelta << ","
                    << pooledFeatures.topEndHeavyRatio << ","
                    << pooledFeatures.lowEndHeavyRatio << ","
                    << pooledFeatures.decayRatio << ","
                    << pooledFeatures.deltaEnergyWeight << ","
                    << pooledFeatures.transientZcr << ","
                    << pooledFeatures.meanCentroidNoBass << ","
                    << pooledFeatures.highpassZcr << std::endl;
        }
        
        DBG("Hat Probabilities:");
        DBG("Kick Probabilities:");
        DBG("Snare Probabilities:");
        
        double hatLikelihood   = calculateClassLikelihood(pooledFeatures, hatParams);
        double kickLikelihood  = calculateClassLikelihood(pooledFeatures, kickParams);
        double snareLikelihood = calculateClassLikelihood(pooledFeatures, snareParams);

        double totalLikelihood = hatLikelihood + kickLikelihood + snareLikelihood;

        ClassificationResult result;

        // ========================================================
        // NORMALIZATION (Convert to percentages)
        // ========================================================
        if (totalLikelihood > 1e-25) // Prevent division by near-zero underflows
        {
            result.hatProbability   = (hatLikelihood / totalLikelihood) * 100.0;
            result.kickProbability  = (kickLikelihood / totalLikelihood) * 100.0;
            result.snareProbability = (snareLikelihood / totalLikelihood) * 100.0;
        }
        else
        {
            // Fallback for extreme outlier signals (assign equal probability)
            result.hatProbability   = 33.33;
            result.kickProbability  = 33.33;
            result.snareProbability = 33.33;
        }
        
        DBG("");
        DBG("Hat Likelihood: " << result.hatProbability);
        DBG("Kick Likelihood: " << result.kickProbability);
        DBG("Snare Likelihood: " << result.snareProbability);

        // Determine the class with the highest probability
        if (result.hatProbability >= result.kickProbability && result.hatProbability >= result.snareProbability)
            return HitType::Hat;
        else if (result.kickProbability >= result.hatProbability && result.kickProbability >= result.snareProbability)
            return HitType::Kick;
        else
            return HitType::Snare;
    }
    
    // Hat: bright/noisy and short
    if (f.zcr > 0.15f) //&& f.durationSec < 0.12f
        return HitType::Hat;

    // Kick: low-frequency dominant, usually longer
    if (f.zcr < 0.08f && f.rms > 0.015f) //&& f.durationSec > 0.15f
        return HitType::Kick;

    // Snare: in-between; often noisy but not as high ZCR as hats
    if (f.zcr >= 0.08f && f.zcr <= 0.15f)
        return HitType::Snare;

    return HitType::Unknown;
}

const char* HitClassifier::toString(HitType t)
{
    switch (t)
    {
        case HitType::Kick:   return "Kick";
        case HitType::Snare:  return "Snare";
        case HitType::Hat:    return "Hat";
        default:              return "Unknown";
    }
}
