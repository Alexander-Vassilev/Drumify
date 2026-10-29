/*
  ==============================================================================

    InputProcessor.cpp
    Created: 31 Jan 2026 11:04:31am
    Author:  Alexander Vassilev

  ==============================================================================
*/

#include "InputProcessor.h"
#include "hitClassifier.h"
#include "JuceHeader.h"
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
    // Classification now happens as each hit closes, so the results are cleared
    // here. clear() keeps the capacity, which prepare() took once - reserving
    // here instead would allocate, and reset() runs on the audio thread at the
    // start of a take.
    classifiedHits.clear();

    storedHitsIndex = 0;
    currHitIndex = 0;
    isActivated = false;
    onsetCounter = 0;
    offsetCounter = 0;
    previousAmp = 0.0f;
    baselineAmp = 0;
    
    onsetDetector.reset();
}

void InputProcessor::processSample(float sample, float amp, bool externalTrigger)
{
    if (storedHitsIndex >= numHits)
        return;

    currSample++;
    int compensatedCurrSample = std::max(0, currSample - totalDelay);

    // --- Update pre-roll buffer (always) ---
    preRoll[preRollIndex] = sample;
    preRollIndex = (preRollIndex + 1) % preRollSamples;

    // ===============================
    // ONSET TRIGGER LOGIC (Retrospective)
    // ===============================
    if (externalTrigger)
    {
        if (!isActivated)
        {
            DBG("Sample " << compensatedCurrSample << ": new hit triggered externally");
            isActivated = true;
            currHitIndex = 0;

            auto& hit = storedHits[storedHitsIndex];
            hit.onsetSample = compensatedCurrSample;
            hit.buffer.setSize(1, samplesPerHit);
            hit.buffer.clear();
            writePtr = hit.buffer.getWritePointer(0);

            // --- Copy Retrospective Pre-roll ---
            // Grabs the past 1756 samples from the circular buffer
            for (int i = 0; i < totalDelay; ++i)
            {
                int delay = totalDelay - i; // Ranges from 1756 down to 1
                int idx = (preRollIndex - delay + preRollSamples) % preRollSamples;
                writePtr[currHitIndex] = preRoll[idx];
                currHitIndex++;
            }
        }
        else
        {
            // --- MID-HIT RE-TRIGGER LOGIC ---
            // Ensure we don't trim into negative indices during a split
            if (currHitIndex > totalDelay)
            {
                int hitLength = currHitIndex - totalDelay;
                
                if (hitLength > minHitLength)
                {
                    DBG("Sample " << compensatedCurrSample << ": re-triggered new hit mid-signal (retrospective)");
                    auto& oldHit = storedHits[storedHitsIndex];
                    oldHit.hitLength = hitLength;
                    classifyHit(storedHitsIndex);
                    storedHitsIndex++;
                }

                if (storedHitsIndex >= numHits)
                {
                    isActivated = false;
                }
                else
                {
                    // Start the new hit
                    currHitIndex = 0;
                    auto& newHit = storedHits[storedHitsIndex];
                    newHit.onsetSample = compensatedCurrSample;
                    newHit.buffer.setSize(1, samplesPerHit);
                    newHit.buffer.clear();
                    writePtr = newHit.buffer.getWritePointer(0);

                    // Copy retrospective pre-roll
                    for (int i = 0; i < totalDelay; ++i)
                    {
                        int delay = totalDelay - i;
                        int idx = (preRollIndex - delay + preRollSamples) % preRollSamples;
                        writePtr[currHitIndex] = preRoll[idx];
                        currHitIndex++;
                    }

                    offsetCounter = 0;
                }
            }
        }
    }

    // ===============================
    // RECORDING LOGIC
    // ===============================
    if (isActivated)
    {
        if (currHitIndex < samplesPerHit)
        {
            writePtr[currHitIndex] = sample;
            currHitIndex++;
        }
            
        // ===============================
        // OFFSET LOGIC (sustained below threshold)
        // ===============================
        if (amp < offsetThreshold)
        {
            offsetCounter++;
            if (offsetCounter >= minOffsetSamples)
            {
                if (currHitIndex > minHitLength)
                {
                    DBG("Sample " << compensatedCurrSample << ": curr sample hit complete");
                    auto& hit = storedHits[storedHitsIndex];
                    hit.hitLength = currHitIndex;

                    classifyHit(storedHitsIndex);
                    storedHitsIndex++;
                    isActivated = false;
                    offsetCounter = 0;
                }
            }
        }
        else
        {
            offsetCounter = 0;  // Reset if amplitude goes back up
        }
    }
}

/** Classifies one stored hit and appends the result.

    Called from processSample, so during a live take this runs on the audio
    thread, inside the same block that completed the hit. The file writes
    classifyStoredHits used to do - the STFT dump, the per-hit wav, the feature
    CSV - are deliberately absent: blocking I/O here would risk a dropout.
    extractFeatures and HitClassifier::classify still allocate internally, which
    is the remaining real-time hazard.
*/
void InputProcessor::prepare(double sampleRate)
{
    analysisSampleRate = sampleRate;

    hitClassifier.prepare (sampleRate);

    // The spectrum buffer is the big one - maxStftFrames frames of numBins
    // floats, about a megabyte. Taken here so the first hit of a take costs no
    // more than any other.
    reusableFeatures.stftData.reserve ((size_t) HitFeatures::maxStftFrames);

    // storedHitsIndex caps at numHits, so this capacity is always enough and
    // reset() need only clear.
    classifiedHits.reserve (numHits);
}

void InputProcessor::classifyHit(int index)
{
    if (index < 0 || index >= numHits)
        return;

    const auto& hit = storedHits[(size_t) index];

    // One HitFeatures, reused: its spectrum buffer keeps the capacity it grew
    // on the first hit, so nothing here allocates afterwards.
    hitClassifier.extractFeaturesInto(hit.buffer, hit.hitLength, analysisSampleRate, reusableFeatures);
    const auto& features = reusableFeatures;

    ClassifiedHit classified;
    classified.hitIndex = index;
    classified.onsetSample = hit.onsetSample;
    classified.type = HitClassifier::classify(features, csvFile, classBias);
    classified.rms = features.rms;
    classified.zcr = features.zcr;
    classified.durationSec = features.durationSec;

    classifiedHits.push_back(classified);
}

void InputProcessor::flush()
{
    if (isActivated)
    {
        // Force-finalize the final hanging hit
        if (currHitIndex > minHitLength)
        {
            DBG("File finished: Forcing finalization of last hit. Length: " << currHitIndex);
            
            auto& hit = storedHits[storedHitsIndex];
            hit.hitLength = currHitIndex;

            classifyHit(storedHitsIndex);
            storedHitsIndex++;
        }
        
        // Reset state
        isActivated = false;
        offsetCounter = 0;
    }
}

juce::AudioBuffer<float> InputProcessor::hitsToBuffer() {
    int totalSamples = 0;
    const int samplesBetweenHits = 40000;
    
    for (int hit = 0; hit < storedHitsIndex; hit++) {
        totalSamples += storedHits[hit].hitLength + samplesBetweenHits;  // hit + padding
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
        
        for (int padding = 0; padding < samplesBetweenHits; padding++) {
            retBuffWritePtr[currIndex] = 0;
            currIndex++;
        }
    }
    //DBG("Hits conveted to bufer");
    
    return retBuffer;
};

/** Writes one detected hit out as a mono wav, named for the file it came from,
    its position in that file, and what it was classified as - so a folder of
    these can be skimmed to see where the classifier goes wrong.
*/
static void writeExtractedHit (const MouthHit& hit, HitType type, int index,
                               const juce::String& sourceName, double sampleRate)
{
    const int numSamples = juce::jmin (hit.hitLength, hit.buffer.getNumSamples());

    if (numSamples <= 0)
        return;

    const auto folder = InputProcessor::getExtractedHitsFolder();

    if (! folder.createDirectory())
        return;

    // Live input has no source file, and re-running one file overwrites its own
    // hits rather than piling up duplicates.
    const auto stem = juce::File::createLegalFileName (
        sourceName.isNotEmpty() ? sourceName.upToLastOccurrenceOf (".", false, false)
                                : juce::String ("recording"));

    const auto destination = folder.getChildFile (stem
                                                    + "_" + juce::String (index).paddedLeft ('0', 3)
                                                    + "_" + HitClassifier::toString (type)
                                                    + ".wav");
    destination.deleteFile();

    auto fileStream = std::make_unique<juce::FileOutputStream> (destination);

    if (! fileStream->openedOk())
        return;

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
    juce::WavAudioFormat wavFormat;

    auto writer = wavFormat.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                        .withSampleRate (sampleRate > 0.0 ? sampleRate : 44100.0)
                                                        .withNumChannels (1)
                                                        .withBitsPerSample (24));

    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (hit.buffer, 0, numSamples);
}

void InputProcessor::classifyStoredHits(double sampleRate)
{
    // Hits are classified by classifyHit as each one closes, so this only
    // re-runs that pass - for an offline sweep, or after the class bias
    // changes. The STFT text dump and the per-hit wav write that used to live
    // here are commented out; they were the bulk of the cost, and classifyHit
    // now runs on the audio thread during a live take.
    //
    // const auto stftFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
    //                           .getChildFile("DrumifyAnalysis")
    //                           .getChildFile("stft_bins.txt");
    // stftFile.getParentDirectory().createDirectory();
    // std::ofstream stftOut(...);   // one row per frame, one column per bin
    // writeExtractedHit(hit, classified.type, i, currFileName, sampleRate);

    analysisSampleRate = sampleRate;

    classifiedHits.clear();
    classifiedHits.reserve(storedHitsIndex);

    DBG("---- Classifying Stored Hits ----");
    DBG(storedHitsIndex << " Hits classified");

    double totalClassificationMs = 0.0;

    for (int i = 0; i < storedHitsIndex; ++i)
    {
        const auto started = juce::Time::getHighResolutionTicks();
        classifyHit(i);
        totalClassificationMs += juce::Time::highResolutionTicksToSeconds(
                                     juce::Time::getHighResolutionTicks() - started) * 1000.0;
    }

    if (storedHitsIndex > 0)
        DBG("---- Classified " << storedHitsIndex << " hits in "
                               << juce::String(totalClassificationMs, 3) << " ms, "
                               << juce::String(totalClassificationMs / storedHitsIndex, 3)
                               << " ms each ----");

    juce::ignoreUnused(totalClassificationMs);
}


class JuceHfcDetector
{
public:
    // 512 or 1024 is typical for fast transient detection
    JuceHfcDetector(int fftOrder)
        : fft(fftOrder)
        , window(fft.getSize(), juce::dsp::WindowingFunction<float>::hann)
    {
        fftBuffer.resize(fft.getSize() * 2, 0.0f);
    }

    // Call this whenever you have collected a full window of samples (e.g., 512 samples)
    float calculateHfc(const float* sampleWindow)
    {
        // 1. Copy samples and apply Hann window
        std::memcpy(fftBuffer.data(), sampleWindow, fft.getSize() * sizeof(float));
        window.multiplyWithWindowingTable(fftBuffer.data(), fft.getSize());

        // 2. Perform forward FFT (in-place)
        fft.performRealOnlyForwardTransform(fftBuffer.data());

        // fftBuffer now contains interleaved complex numbers: [real0, imag0, real1, imag1, ...]
        float hfc = 0.0f;
        int numBins = fft.getSize() / 2;

        // 3. Sum the weighted magnitude of each bin
        for (int bin = 0; bin < numBins; ++bin)
        {
            float real = fftBuffer[2 * bin];
            float imag = fftBuffer[2 * bin + 1];
            float magnitude = std::sqrt(real * real + imag * imag);

            // Weight linearly by the bin index (HFC formula)
            hfc += magnitude * static_cast<float>(bin);
        }

        return hfc;
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> fftBuffer;
};
