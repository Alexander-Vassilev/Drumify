/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor. and also this comment

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>
//void runDrumClassifierSmokeTests();

//==============================================================================
HackBrownAudioProcessor::HackBrownAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    formatManager.registerBasicFormats();
}

HackBrownAudioProcessor::~HackBrownAudioProcessor()
{
}

//==============================================================================
const juce::String HackBrownAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HackBrownAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool HackBrownAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool HackBrownAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double HackBrownAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HackBrownAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int HackBrownAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HackBrownAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String HackBrownAudioProcessor::getProgramName(int index)
{
    return {};
}

void HackBrownAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================

void HackBrownAudioProcessor::reset()
{
    isFifoFilled = true;
    currSampleInFile = 0;
    samplesAccumulated = 0;
    fifoIndex = 0;
    std::fill(std::begin(fifoBuffer), std::end(fifoBuffer), 0.0f);
    
    envelopeFollower.reset();
    inputProcessor.reset();
    statisticalDetector.reset();
}

void HackBrownAudioProcessor::getLongestSampleLengthInSamples()
{
    int maxLength = 0;

    // Loop backward or forward through all sounds currently in the synth
    for (int i = 0; i < drumSynth.getNumSounds(); ++i)
    {
        // Try to cast the base juce::SynthesiserSound to a juce::SamplerSound
        if (auto* samplerSound = dynamic_cast<juce::SamplerSound*> (drumSynth.getSound(i).get()))
        {
            // Get the shared pointer to the audio buffer holding the actual audio data
            if (auto audioData = samplerSound->getAudioData())
            {
                int currentLength = audioData->getNumSamples();
                
                if (currentLength > maxLength)
                    maxLength = currentLength;
            }
        }
    }

    numSamplesLongestSound = maxLength;
}

void HackBrownAudioProcessor::loadSampleFromReader (std::unique_ptr<juce::AudioFormatReader> reader,
                                                   const juce::String& sampleName,
                                                   int midiNote)
{
    if (reader == nullptr)
        return;

    juce::BigInteger noteRange;
    noteRange.setBit (midiNote);

    const double attack = 0.001;
    const double release = 0.05;

    DrumSamplerSound::Ptr sound = new DrumSamplerSound (sampleName,
                                                        *reader,
                                                        noteRange,
                                                        midiNote,
                                                        attack,
                                                        release,
                                                        10.0);

    DBG("Midi note when loading: " << midiNote);

    // Clear old sounds assigned to this midiNote so they don't stack up
    for (int i = drumSynth.getNumSounds() - 1; i >= 0; --i)
    {
        if (auto* oldSound = dynamic_cast<juce::SamplerSound*> (drumSynth.getSound(i).get()))
        {
            if (oldSound->appliesToNote (midiNote))
                drumSynth.removeSound (i);
        }
    }

    drumSynth.addSound (sound.get());

    // addSound appends, so the synth's ordering no longer matches the drums.
    // This map is what the renderer reads instead.
    drumSoundsByNote[midiNote] = sound;
}

DrumSamplerSound::Ptr HackBrownAudioProcessor::getSoundForNote (int midiNote) const
{
    const auto found = drumSoundsByNote.find (midiNote);

    return found != drumSoundsByNote.end() ? found->second : nullptr;
}

std::unique_ptr<juce::AudioFormatReader> HackBrownAudioProcessor::createReaderForFile (const juce::File& file)
{
    auto stream = std::make_unique<juce::FileInputStream> (file);

    if (! stream->openedOk())
    {
        DBG ("Failed to open file stream: " + file.getFullPathName());
        return nullptr;
    }

    // formatManager.createReaderFor returns a raw pointer, so we wrap it in a unique_ptr immediately.
    // We use std::move(stream) because the manager takes ownership of the underlying file stream.
    return std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (std::move (stream)));
}

void HackBrownAudioProcessor::analyzeLoadedDrumLoop (const juce::AudioBuffer<float>& loopBuffer)
{
    // 1. Reset your DSP components so old history is erased
    reset();
    
    const int totalSamples = loopBuffer.getNumSamples();
    const int chunkSize = getBlockSize();

    // Sum to mono rather than analysing channel 0 alone: anything panned hard to
    // one side - a hat or ride off to one edge - would otherwise be missed
    // entirely, and a file with a near-silent left channel would yield no onsets.
    juce::AudioBuffer<float> monoBuffer (1, totalSamples);
    monoBuffer.clear();

    const int numChannels = loopBuffer.getNumChannels();

    if (numChannels <= 0 || totalSamples <= 0)
        return;

    for (int channel = 0; channel < numChannels; ++channel)
        monoBuffer.addFrom (0, 0, loopBuffer, channel, 0, totalSamples,
                            1.0f / static_cast<float> (numChannels));

    const float* totalInputData = monoBuffer.getReadPointer (0);

    // This is the "original input" for an uploaded loop, so keep it for the
    // preview the same way recordAudio keeps a live take.
    {
        const int toKeep = juce::jmin (totalSamples, capturedInput.getNumSamples());

        if (toKeep > 0)
            capturedInput.copyFrom (0, 0, monoBuffer, 0, 0, toKeep);

        capturedInputLength.store (toKeep);
    }

    for (int startSample = 0; startSample < totalSamples; startSample += chunkSize)
    {
        int samplesToProcess = std::min (chunkSize, totalSamples - startSample);
        const float* chunkPtr = totalInputData + startSample;
        classifyAudioBlock (0, chunkPtr, samplesToProcess);
    }
    
    inputProcessor.flush();
    reconstructLoopFromHits();
    DBG ("Drum loop analysis finished!");
}

void HackBrownAudioProcessor::processUploadedLoop(const juce::File& file)
{
    auto reader = createReaderForFile (file);
    inputProcessor.currFileName = file.getFileName();
    
    if (reader == nullptr)
    {
        DBG ("Could not create reader for file: " + file.getFileName());
        return;
    }
    
    const double sourceSampleRate = reader->sampleRate;
    const double targetSampleRate = 44100.0;
    
    const double ratio = sourceSampleRate / targetSampleRate;
    const int targetLength = static_cast<int> (std::ceil (static_cast<double> (reader->lengthInSamples) / ratio));

    // 2. Load the original file data into a temporary buffer
    juce::AudioBuffer<float> originalBuffer (static_cast<int> (reader->numChannels),
                                             static_cast<int> (reader->lengthInSamples));
    
    reader->read (&originalBuffer,
                  0,                                // dest start sample
                  static_cast<int> (reader->lengthInSamples),
                  0,                                // source start sample
                  true,                             // fill left
                  true);                            // fill right

    // 3. Prepare the final buffer at exactly 44.1 kHz
    juce::AudioBuffer<float> normalizedBuffer (static_cast<int> (reader->numChannels), targetLength);
    
    // If the file is already 44.1 kHz, we can skip resampling and copy directly
    if (std::abs (sourceSampleRate - targetSampleRate) < 0.01)
    {
        normalizedBuffer.makeCopyOf (originalBuffer);
    }
    else
    {
        DBG ("Resampling file from " << sourceSampleRate << " Hz to 44100 Hz...");
        
        juce::LagrangeInterpolator resampler;
        
        for (int channel = 0; channel < originalBuffer.getNumChannels(); ++channel)
        {
            resampler.reset();
            
            // Resample the channel into our normalized buffer
            resampler.process (ratio,
                               originalBuffer.getReadPointer (channel),
                               normalizedBuffer.getWritePointer (channel),
                               targetLength);
        }
    }

    // 5. Pass the newly populated buffer to your analysis function
    analyzeLoadedDrumLoop (normalizedBuffer);
}

void HackBrownAudioProcessor::loadSampleFromFile(const juce::File& file, int midiNote)
{
    auto reader = createReaderForFile(file);
    loadSampleFromReader(std::move (reader), file.getFileNameWithoutExtension(), midiNote);
}

//the function that wraps BinaryData in a MemoryInputStream
void HackBrownAudioProcessor::loadSampleFromBinaryData(const juce::String& name, const void* data, int dataSize, int midiNote)
{
    auto stream = std::make_unique<juce::MemoryInputStream>(data, (size_t)dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(stream)));

    loadSampleFromReader(std::move(reader), name, midiNote);
}



juce::AudioBuffer<float> loadAudioFile(const juce::File& file) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        juce::AudioBuffer<float> buffer(reader->numChannels,
            (int)reader->lengthInSamples);
        reader->read(&buffer, 0, (int)reader->lengthInSamples, 0, true, true);
        return buffer;
    }

    return juce::AudioBuffer<float>();
}

//==============================================================================
void HackBrownAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    //static bool ranTests = false;
    //if (!ranTests)
    //{
    //    ranTests = true;
    //    runDrumClassifierSmokeTests();
    //}
    //setLatencySamples(inputProcessor.hitClassifier.fft.getLatencyInSamples());
    inputProcessor.hitClassifier.fft.reset();
    
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    //spec.numChannels = getTotalNumOutputChannels();
    spec.numChannels = 1;
    
    auto coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, 1000, 2.0, 8.0f);
    *bellFilter.coefficients = *coefficients;
    bellFilter.prepare(spec);
    bellFilter.reset();
    
    envelopeFollower.prepare(spec);
    envelopeFollower.setAttackTime(15.0f);
    envelopeFollower.setReleaseTime(80.0f);
    sineGenerator.prepare(sampleRate, samplesPerBlock);

    drumSynth.clearVoices();

    for (int i = 0; i < 16; ++i)
        drumSynth.addVoice(new juce::SamplerVoice());

    drumSynth.setCurrentPlaybackSampleRate(sampleRate);

    // Allocated here, on the message thread, so recordAudio never has to.
    capturedInput.setSize(1, juce::jmax(1, (int)(maxCapturedInputSeconds * sampleRate)));
    capturedInput.clear();
    capturedInputLength.store(0);
    wasRecording = false;

    drumMidiMap[kick]  = 36;
    drumMidiMap[snare] = 38;
    drumMidiMap[hat]   = 42;

    // Sounds deliberately survive prepareToPlay. A host calls it again whenever
    // the sample rate or block size changes, and clearing here would silently
    // throw away samples the user had loaded. SamplerVoice resamples against the
    // rate set above, so sounds loaded at the old rate stay valid; only the
    // slots that are still empty need the built-in defaults.
    if (getSoundForNote (drumMidiMap[kick]) == nullptr)
        loadSampleFromBinaryData("Kick", BinaryData::Kick_wav, BinaryData::Kick_wavSize, drumMidiMap[kick]);

    if (getSoundForNote (drumMidiMap[snare]) == nullptr)
        loadSampleFromBinaryData("Snare", BinaryData::Snare_wav, BinaryData::Snare_wavSize, drumMidiMap[snare]);

    if (getSoundForNote (drumMidiMap[hat]) == nullptr)
        loadSampleFromBinaryData("Hat", BinaryData::Hat_wav, BinaryData::Hat_wavSize, drumMidiMap[hat]);

    getLongestSampleLengthInSamples();
    
    currentSampleRate = sampleRate;
    
    logFile.open(file.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);
    //makeTestRender(); //TEMP, remove it later!!
}

void HackBrownAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool HackBrownAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

bool HackBrownAudioProcessor::shouldReplaceNote (int midiNote) const
{
    if (midiNote == drumMidiMap.at (kick))  return replaceKick;
    if (midiNote == drumMidiMap.at (snare)) return replaceSnare;
    if (midiNote == drumMidiMap.at (hat))   return replaceHat;

    // Anything unclassified keeps falling back to the hat sample as before;
    // the checkboxes only govern the three named drums.
    return true;
}

juce::AudioBuffer<float> HackBrownAudioProcessor::renderDrumLoopOffline(
    const std::vector<DrumEventAbs>& events,
    double sampleRate,
    int outputNumSamples)
{
    juce::AudioBuffer<float> out;
    out.setSize(2, outputNumSamples);
    out.clear();

    // Hits kept at their original audio render here, apart from the samples, so
    // the normalisation below can measure and scale the samples alone. The
    // originals may already sit at full scale; running them through the same
    // gain would push them into clipping.
    juce::AudioBuffer<float> kept;
    kept.setSize(1, outputNumSamples);
    kept.clear();

    int offset = 0;

    if (events.size() >= 1) {
        offset = events[0].sampleIndex * playbackSpeed;
    }
    
    for (int i = 0; i < events.size(); i++) {
        //DBG("num events" << events.size());
        bool skipFilter = true;

        juce::AudioBuffer<float> copier;
        const bool replaced = shouldReplaceNote (events[i].midiNote);

        if (! replaced)
        {
            // This drum is switched off: keep the hit's own audio in the loop.
            // It is spliced in at full level - it is already at its recorded
            // level - with a short fade at each end, because a hard cut into
            // and out of the rendered material would click.
            const int hitIndex = events[i].hitIndex;

            if (hitIndex < 0 || hitIndex >= inputProcessor.storedHitsIndex)
                continue;

            const auto& hit = inputProcessor.storedHits[(size_t) hitIndex];
            const int hitLength = juce::jmin (hit.hitLength, hit.buffer.getNumSamples());

            if (hitLength <= 0 || hit.buffer.getNumChannels() == 0)
                continue;

            copier.setSize (1, hitLength);
            copier.copyFrom (0, 0, hit.buffer, 0, 0, hitLength);

            const int fadeIn = juce::jmin (hitLength / 2, (int) (unreplacedHitFadeInSeconds * sampleRate));
            const int fadeOut = juce::jmin (hitLength / 2, (int) (unreplacedHitFadeOutSeconds * sampleRate));

            if (fadeIn > 0 || fadeOut > 0)
            {
                copier.applyGainRamp (0, 0, fadeIn, 0.0f, 1.0f);
                copier.applyGainRamp (0, hitLength - fadeOut, fadeOut, 1.0f, 0.0f);
            }
        }
        else
        {
            // Look the drum up by its note. Unclassified hits fall back to the hat,
            // as they did when this switched on the note directly.
            auto sound = getSoundForNote (events[i].midiNote);

            if (sound == nullptr)
                sound = getSoundForNote (drumMidiMap[hat]);

            if (sound == nullptr || sound->getAudioData() == nullptr)
                continue;

            copier = *sound->getAudioData();
            copier.applyGainRamp(0, 0, copier.getNumSamples(), events[i].velocity01, events[i].velocity01);
        }

        if (!skipFilter) {
            if (events[i].filterOn) {
                bellFilter.reset();
                juce::dsp::AudioBlock<float> block(copier);
                juce::dsp::ProcessContextReplacing<float> context(block);
                bellFilter.process(context);
            }
        }
        //DBG("attempting copy");
        //DBG("start sample: " << events[i].sampleIndex * playbackSpeed - offset);
        const int startSample = (int) (events[i].sampleIndex * playbackSpeed) - offset;

        // outputNumSamples is derived from unscaled onsets, so a playbackSpeed
        // above 1 can push a hit past the end of the buffer. Clamp the copy
        // rather than running off it.
        const int numToCopy = juce::jmin (copier.getNumSamples(), outputNumSamples - startSample);

        if (startSample < 0 || numToCopy <= 0)
            continue;

        auto writePtr = (replaced ? out : kept).getWritePointer(0);
        auto readPtr = copier.getReadPointer(0);
        
        for (int i = 0; i < numToCopy; i++) {
            writePtr[startSample + i] += readPtr[i];
        }
        
        //out.copyFrom(0, startSample, copier, 0, 0, numToCopy);
        //DBG("applying gain ramp");
        
        //out.applyGainRamp(0, startSample, numToCopy, events[i].velocity01, events[i].velocity01);
        //DBG("copied");
    }

    // Bring the loudest sample hit up to full scale. velocity01 is a raw RMS with
    // no reference, so without this the whole loop sat at whatever that RMS
    // happened to be - typically 10-20 dB down. Per-hit dynamics are preserved;
    // only the overall level moves.
    if (outputNumSamples > 0)
    {
        const float peak = out.getMagnitude(0, 0, outputNumSamples);

        if (peak > 0.0f)
            out.applyGain(0, 0, outputNumSamples, 1.0f / peak);

        out.addFrom(0, 0, kept, 0, 0, outputNumSamples);
    }

    return out;
}

void HackBrownAudioProcessor::classifyAudioBlock (int channel, const float* inputData, int numSamples)
{
    if (channel != 0) return;
    
    for (int i = 0; i < numSamples; i++)
    {
        currSampleInFile++;
        float sample = inputData[i];
        
        fifoBuffer[fifoIndex] = sample;
        fifoIndex++;
        fifoIndex &= bufferMask;
        
        samplesAccumulated++;
        
        bool onsetConfirmedThisSample = false;
        
        // Because we primed the FIFO, we trigger our very first FFT after only 256 samples
        if (samplesAccumulated >= fftHopSize)
        {
            float analysisWindow[fftWindowSize];
            
            for (int j = 0; j < fftWindowSize; j++) {
                analysisWindow[j] = fifoBuffer[(fifoIndex + j) & bufferMask];
            }
            
            float odfValue = complexOnsetDetector.processFrame(analysisWindow, currSampleInFile);
            onsetConfirmedThisSample = statisticalDetector.processSample(odfValue, currSampleInFile);
            
            logFile << odfValue << std::endl;
            //DBG("odf: " << odfValue);
            
            if (onsetConfirmedThisSample) {
                int compensatedOnset = currSampleInFile;
                DBG("Onset detected at sample index: " << compensatedOnset);
            }
            
            samplesAccumulated = 0;
        }

        // Process every sample (including the very first ones) into the InputProcessor
        float amp = envelopeFollower.processSample (0, sample);
        inputProcessor.processSample (sample, amp, onsetConfirmedThisSample);
    }
}

void HackBrownAudioProcessor::recordAudio(juce::AudioBuffer<float>& buffer) {
    isPlayingRendered = true;
    recordingStarted.store(true);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());


    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        juce::ignoreUnused(channelData);
    }

    auto* inputData = buffer.getReadPointer(0);

    // Keep the raw input for the preview. copyFrom does not allocate, and the
    // length is clamped to the buffer sized in prepareToPlay, so this is safe on
    // the audio thread; a take longer than the cap simply stops being kept.
    {
        const int written = capturedInputLength.load();
        const int room = capturedInput.getNumSamples() - written;
        const int toKeep = juce::jmin(buffer.getNumSamples(), room);

        if (toKeep > 0 && totalNumInputChannels > 0)
        {
            capturedInput.copyFrom(0, written, buffer, 0, 0, toKeep);
            capturedInputLength.store(written + toKeep);
        }
    }

    for (int channel = 0; channel < 1; ++channel) {
        float* channelData = buffer.getWritePointer(channel);

        // Only process if we have a corresponding input channel
        if (channel < totalNumInputChannels) {
            classifyAudioBlock(channel, inputData, buffer.getNumSamples());
        }
        else {
            // Clear any extra output channels
            buffer.clear(channel, 0, buffer.getNumSamples());
        }
    }

    buffer.clear();
}

void HackBrownAudioProcessor::playAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    buffer.clear();

    const auto& source = previewingInput.load() ? inputPreviewBuffer : renderedDrumBuffer;

    // Nothing captured or rendered yet, so there is nothing to preview.
    if (source.getNumChannels() == 0 || source.getNumSamples() == 0) {
        isPlayingRendered = false;
        isPlaybackOn.store(false);
        renderedReadPos = 0;
        midiMessages.clear();
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const int remaining = source.getNumSamples() - renderedReadPos;
    const int toCopy = juce::jmin(numSamples, remaining);

    auto* inputData = source.getReadPointer(0);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        //buffer.copyFrom(ch, 0, renderedDrumBuffer, juce::jmin(ch, renderedDrumBuffer.getNumChannels()-1),
        //                renderedReadPos, toCopy);
        float* channelData = buffer.getWritePointer(ch);

        for (int sample = 0; sample < toCopy; sample++) {
            channelData[sample] = inputData[sample + renderedReadPos];
        }

        for (int sample = toCopy; sample < buffer.getNumSamples(); sample++) {
            channelData[sample] = 0.0f;
        }
    }

    renderedReadPos += toCopy;

    if (renderedReadPos >= source.getNumSamples()) {
        DBG("Stop playback");
        isPlayingRendered = false;
        isPlaybackOn.store(false);
        renderedReadPos = 0;
    }

    midiMessages.clear();
}

void HackBrownAudioProcessor::startPreview(PreviewSource source) {
    // Stop first so the audio thread cannot be reading while the buffer is swapped.
    isPlaybackOn.store(false);

    if (source == PreviewSource::input) {
        // Snapshot the captured input rather than reading it in place, so a
        // take that starts mid-preview cannot overwrite what is being played.
        const int length = capturedInputLength.load();

        inputPreviewBuffer.setSize(1, juce::jmax(0, length));

        if (length > 0)
            inputPreviewBuffer.copyFrom(0, 0, capturedInput, 0, 0, length);
    }

    previewingInput.store(source == PreviewSource::input);
    renderedReadPos = 0;
    isPlaybackOn.store(true);
}

void HackBrownAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    if (playprint) {
        DBG("PLAYING");
        DBG("");
        DBG("");
        DBG("");
        DBG("");
        playprint = false;
    }
    
    auto totalNumInputChannels  = getTotalNumInputChannels();
    
    // A host may legitimately call us with an empty buffer or no input bus - when
    // the plugin sits on a track with no input, or while it is being probed - so
    // this must bow out quietly rather than assert.
    if (buffer.getNumSamples() == 0 || totalNumInputChannels == 0) {
        buffer.clear();
        return;
    }
    
    const bool recording = recordingEnabled.load();

    // A fresh take starts a fresh capture. Edge-detected here rather than in the
    // editor so it stays in step with what the audio thread actually recorded.
    if (recording && ! wasRecording)
    {
        capturedInputLength.store(0);

        // A new take replaces the last one, as an uploaded loop does. Without
        // this, storedHitsIndex and currSampleInFile carry on from where the
        // previous take stopped, so its hits are appended after the old ones.
        // Nothing in reset() allocates, so it is safe here on the audio thread.
        reset();
    }

    wasRecording = recording;

    if (recording) {
        recordAudio(buffer);
    } else if (isPlaybackOn.load()) {
        playAudio(buffer, midiMessages);
        
        return;
    } else {
        
        buffer.clear();
    }
}

void HackBrownAudioProcessor::reconstructLoopFromHits() {
    // algorithm classifies/do classification
    isPlaybackOn.store(false);
    inputProcessor.classifyStoredHits(getSampleRate());
    //DBG("classified");
    //DBG(inputProcessor.classifiedHits[0].durationSec);
    //DBG(inputProcessor.classifiedHits[0].rms);
    buildDrumBuffer();
    //DBG("2");
    inputProcessor.hitsToBuffer();
    //DBG("3");
    
    DBG("---- Editor sees classified hits ----");
    for (const auto& ch : inputProcessor.classifiedHits) //hits are stored in inputProcessor.classifiedhits
    {
        DBG("Hit index " << ch.hitIndex
            << " classified as "
            << HitClassifier::toString(ch.type));
    }
}

//==============================================================================
bool HackBrownAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* HackBrownAudioProcessor::createEditor()
{
    return new HackBrownAudioProcessorEditor(*this);
}

//==============================================================================
void HackBrownAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void HackBrownAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HackBrownAudioProcessor();
}

std::vector<ClassifiedHit> HackBrownAudioProcessor::getTimedHits() const
{
    std::vector<ClassifiedHit> timed = inputProcessor.classifiedHits;

    if (quantizeEnabled) {
        DBG("Quantising!");
        int quantizeUnitInSamples = (60.0f / quantizeBpm) * quantizeDivision * 4 * currentSampleRate;
        int halfQuantizeUnit = quantizeUnitInSamples >> 1;
        int numRemovedHits = 0;
        
        for (auto& hit : timed) {
            hit.hitIndex -= numRemovedHits;
            int relativeStartSample = hit.onsetSample - startSample;
            int unitIndex = std::floor(static_cast<float>(relativeStartSample) / static_cast<float>(quantizeUnitInSamples));
            int startingSample = unitIndex * quantizeUnitInSamples;
            if (relativeStartSample - startingSample > halfQuantizeUnit) startingSample += quantizeUnitInSamples;
            hit.onsetSample = startingSample + startSample;
        }
    }
    
    // Quantisation goes here: move each hit's onsetSample onto the grid. Work
    // in seconds relative to the first hit (the loop's origin), and round back
    // to a sample index once at the end. Stretching is applied downstream by
    // the renderer and the MIDI writer, so snap in the original tempo.

    return timed;
}

void HackBrownAudioProcessor::buildDrumBuffer() {
    std::vector<DrumEventAbs> events;
    const double sr = currentSampleRate;
    int lastSampleHit = 0;
    float lastSize = 0;

    for (auto processedHit : getTimedHits()) {
        lastSampleHit = processedHit.onsetSample;
        lastSize = processedHit.durationSec;
        DBG("while adding processed hits, this is type: " << (int)processedHit.type);
        events.push_back({ processedHit.onsetSample, (int)processedHit.type, processedHit.rms,
                           processedHit.hitIndex });
    }
    
    for (auto e : events) {
        DBG("event");
        DBG("DrumEventAbs -> Index: %d | Note: %d | Vel: %.2f | Filter: %s | Freq: %.1f Hz" <<
                    e.sampleIndex << " " << e.midiNote << " " << e.velocity01);
    }

    const int outLen = int(lastSampleHit + lastSize * sr + numSamplesLongestSound);
    DBG("lastSample");
    DBG(lastSampleHit);
    renderedDrumBuffer = renderDrumLoopOffline(events, sr, outLen);

    renderedReadPos = 0;
}

//====================TEST==========================================================
void HackBrownAudioProcessor::makeTestRender()
{
    std::vector<DrumEventAbs> events;

    const double sr = currentSampleRate;
    events.push_back({ int(0.0 * sr),     36, 1.0f }); // kick at 0s
    events.push_back({ int(0.5 * sr),     38, 1.0f }); // snare at 0.5s
    events.push_back({ int(1.0 * sr),     36, 1.0f }); // kick at 0s
    events.push_back({ int(0.25 * sr),     42, 1.0f }); // hat at 1.0s
    events.push_back({ int(0.75 * sr),     42, 1.0f }); // hat at 1.0s

    const int outLen = int(2.5 * sr); // 1.5s output
    renderedDrumBuffer = renderDrumLoopOffline(events, sr, outLen);

    renderedReadPos = 0;
}
