/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor. and also this comment

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>

// Lets the standalone build pick its own block size. Not pulled in by
// JuceHeader, and harmless in the plugin builds: everything in it is inline,
// and StandalonePluginHolder::getInstance() is null unless this really is the
// standalone app.
#if __has_include(<juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>)
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #define DRUMIFY_CAN_SET_BLOCK_SIZE 1
#else
 #define DRUMIFY_CAN_SET_BLOCK_SIZE 0
#endif
//void runDrumClassifierSmokeTests();

namespace
{
    /** The block size the standalone app asks its audio device for.

        A plugin has no say in this - the host owns the block size and simply
        tells the plugin what it got - so this only ever applies to the
        standalone build. Smaller blocks mean the monitored hits come back
        sooner, at the cost of more callbacks per second.
    */
    constexpr int preferredStandaloneBlockSize = 128;

    /** Asks the standalone app's device for preferredStandaloneBlockSize.

        The holder restores whatever block size was last saved in the app's
        settings file, and that restore happens after the processor is built -
        so this has to run once the message queue is clear rather than in the
        constructor, or it would simply be overwritten. Does nothing in a
        plugin build, and nothing if the device cannot offer that size.
    */
    void requestPreferredBlockSize()
    {
       #if DRUMIFY_CAN_SET_BLOCK_SIZE
        auto* holder = juce::StandalonePluginHolder::getInstance();

        if (holder == nullptr)
            return;

        // Applied at every launch, not just the first. The app saves its audio
        // setup on every clean shutdown, so a "only if nothing is saved" check
        // would stop this ever taking effect after the first run. The cost is
        // that a size picked in the audio dialog lasts for that session only.
        auto& manager = holder->deviceManager;
        auto* device = manager.getCurrentAudioDevice();

        if (device == nullptr)
            return;

        // Devices only accept the sizes they advertise, so asking for one that
        // is not on the list is refused outright. The nearest size that is on
        // it is taken instead; on most CoreAudio devices 128 is there exactly.
        const auto available = device->getAvailableBufferSizes();

        if (available.isEmpty())
            return;

        int chosen = available[0];

        for (const auto size : available)
            if (std::abs (size - preferredStandaloneBlockSize) < std::abs (chosen - preferredStandaloneBlockSize))
                chosen = size;

        auto setup = manager.getAudioDeviceSetup();

        if (setup.bufferSize == chosen)
            return;

        setup.bufferSize = chosen;

        // treatAsChosenDevice is false: this is the app's own default rather
        // than something the user picked in the dialog.
        const auto error = manager.setAudioDeviceSetup (setup, false);

        if (error.isNotEmpty())
            DBG ("Could not set block size to " << chosen << ": " << error);
        else
            DBG ("Standalone block size set to " << chosen);
       #endif
    }
}

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

    // Set here rather than only in prepareToPlay: a host may restore the
    // plugin's state before preparing it, and the restore loads samples by
    // note.
    drumMidiMap[kick]  = 36;
    drumMidiMap[snare] = 38;
    drumMidiMap[hat]   = 42;

    // Deferred, because in the standalone app the audio device is opened after
    // the processor is constructed - asking now would be undone by that.
    juce::MessageManager::callAsync ([] { requestPreferredBlockSize(); });
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

//==============================================================================
BlockTimingLog::BlockTimingLog()
{
    // Started here rather than in start(): the processor is built on the message
    // thread, while prepareToPlay may not be, and a Timer has to begin there.
    // It does nothing until running is set.
    if (enabled)
        startTimerHz (4);
}

BlockTimingLog::~BlockTimingLog()
{
    stopTimer();
    stop();
}

juce::File BlockTimingLog::getFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
             .getChildFile ("DrumifyAnalysis")
             .getChildFile ("blockTimings.csv");
}

void BlockTimingLog::start (double sampleRateToUse, int blockSize)
{
    if (! enabled)
        return;

    stop();

    const auto file = getFile();
    file.getParentDirectory().createDirectory();

    out.open (file.getFullPathName().toStdString(), std::ios::out | std::ios::trunc);

    if (! out.is_open())
        return;

    out << "block,seconds,numSamples,blockMs,budgetMs,loadPercent,averageLoadPercent,recording\n";

    queue.assign ((size_t) capacity, Entry {});
    fifo.reset();

    sampleRate = sampleRateToUse;
    blockIndex = 0;
    samplePos = 0;
    dropped.store (0);

    measurer.reset (sampleRateToUse, blockSize);
    running.store (true);
}

void BlockTimingLog::stop()
{
    if (! running.exchange (false))
        return;

    drain();

    const auto lost = dropped.load();

    if (lost > 0)
        out << "# " << lost << " blocks dropped: the queue filled between drains\n";

    out.close();
}

void BlockTimingLog::record (int numSamples, juce::int64 ticks, bool recording) noexcept
{
    if (! running.load (std::memory_order_relaxed))
        return;

    const auto millis = juce::Time::highResolutionTicksToSeconds (ticks) * 1000.0;

    // Takes a try-lock and skips the sample if it is contended, so it never
    // blocks this thread.
    measurer.registerRenderTime (millis, numSamples);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 > 0)
        queue[(size_t) start1] = { blockIndex, samplePos, numSamples, (float) millis,
                                   (float) measurer.getLoadAsPercentage(), recording };
    else
        dropped.fetch_add (1, std::memory_order_relaxed);

    fifo.finishedWrite (size1);

    ++blockIndex;
    samplePos += numSamples;
}

void BlockTimingLog::timerCallback()
{
    if (running.load())
        drain();
}

void BlockTimingLog::drain()
{
    if (! out.is_open() || sampleRate <= 0.0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    const auto ready = fifo.getNumReady();

    if (ready <= 0)
        return;

    fifo.prepareToRead (ready, start1, size1, start2, size2);

    const auto writeRange = [this] (int start, int size)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto& entry = queue[(size_t) (start + i)];
            const auto budgetMs = 1000.0 * entry.numSamples / sampleRate;

            out << entry.index << ','
                << (double) entry.samplePos / sampleRate << ','
                << entry.numSamples << ','
                << entry.millis << ','
                << budgetMs << ','
                << (budgetMs > 0.0 ? 100.0 * entry.millis / budgetMs : 0.0) << ','
                << entry.averageLoadPercent << ','
                << (entry.recording ? 1 : 0) << '\n';
        }
    };

    writeRange (start1, size1);
    writeRange (start2, size2);

    fifo.finishedRead (size1 + size2);
    out.flush();
}

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
    applySensitivity();

    // inputProcessor.reset() empties classifiedHits, so the count of hits
    // already monitored and the level they were scaled against both belong to
    // a take that no longer exists.
    liveHitsPlayed = 0;
    livePeakRms = 0.0f;

    // Silences anything still decaying from the previous take, so its tail does
    // not bleed into the new one. Takes the synth's own lock; nothing allocates.
    drumSynth.allNotesOff (0, false);
}

void HackBrownAudioProcessor::applySensitivity()
{
    // The slider moves the absolute threshold only - the level the ODF has
    // to reach before a spike counts at all. Its centre is the tuned value,
    // and each half of the travel runs geometrically from there to its end:
    // far left 100000 (almost nothing triggers), far right 500 (almost
    // anything does). The ratio gate stays as tuned.
    const float s = juce::jlimit (0.0f, 1.0f, sensitivity.load());

    const float absolute = s <= 0.5f
        ? statisticalAbsoluteThreshold * std::pow (statisticalAbsoluteThresholdMax / statisticalAbsoluteThreshold, 1.0f - 2.0f * s)
        : statisticalAbsoluteThreshold * std::pow (statisticalAbsoluteThresholdMin / statisticalAbsoluteThreshold, 2.0f * s - 1.0f);

    statisticalDetector.setThresholds (statisticalRatioThreshold, absolute);
}

void HackBrownAudioProcessor::runDetection (const float* data, int numSamples)
{
    const int chunkSize = getBlockSize();

    for (int startSample = 0; startSample < numSamples; startSample += chunkSize)
    {
        const int samplesToProcess = std::min (chunkSize, numSamples - startSample);
        classifyAudioBlock (0, data + startSample, samplesToProcess);
    }

    inputProcessor.flush();
}

void HackBrownAudioProcessor::reanalyseCapturedInput()
{
    // The audio thread owns the detector during a take; the take's own reset
    // will pick the new sensitivity up when it starts.
    if (recordingEnabled.load())
        return;

    const int length = juce::jmin (capturedInputLength.load(), analysisInput.getNumSamples());

    if (length <= 0 || analysisInput.getNumChannels() == 0)
        return;

    isPlaybackOn.store (false);
    reset();
    runDetection (analysisInput.getReadPointer (0), length);
    reconstructLoopFromHits();
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
    // preview the same way recordAudio keeps a live take. The file's own
    // channels are kept, not the mono sum the analysis runs on: this is what
    // the preview plays and what the render lays down under a partly replaced
    // loop, and a stereo file should come back out as it went in.
    {
        const int toKeep = juce::jmin (totalSamples, capturedInput.getNumSamples());

        if (toKeep > 0)
        {
            for (int channel = 0; channel < capturedInput.getNumChannels(); ++channel)
                capturedInput.copyFrom (channel, 0, loopBuffer,
                                        juce::jmin (channel, numChannels - 1), 0, toKeep);

            // The detector keeps the mono sum it has always analysed.
            analysisInput.copyFrom (0, 0, monoBuffer, 0, 0, toKeep);
        }

        capturedInputLength.store (toKeep);
    }

    runDetection (totalInputData, totalSamples);
    reconstructLoopFromHits();
    DBG ("Drum loop analysis finished!");
}

double HackBrownAudioProcessor::bpmFromFileName (const juce::String& fileName)
{
    constexpr double minBpm = 10.0, maxBpm = 220.0;   // the Select BPM slider's range

    const auto text = fileName.toLowerCase();
    const auto length = text.length();

    double first = 0.0;

    for (int i = 0; i < length; ++i)
    {
        if (! juce::CharacterFunctions::isDigit (text[i]))
            continue;

        int end = i;
        while (end < length && juce::CharacterFunctions::isDigit (text[end]))
            ++end;

        const int digits = end - i;

        if (digits == 2 || digits == 3)
        {
            const double value = text.substring (i, end).getDoubleValue();

            if (value >= minBpm && value <= maxBpm)
            {
                // Skip any separator ("128 bpm", "128_bpm", "128-bpm") before
                // looking for the tag itself.
                int tag = end;
                while (tag < length && ! juce::CharacterFunctions::isLetterOrDigit (text[tag]))
                    ++tag;

                if (text.substring (tag, tag + 3) == "bpm")
                    return value;

                if (first == 0.0)
                    first = value;
            }
        }

        i = end;   // the loop's ++i steps past the non-digit that ended the run
    }

    return first;
}

double HackBrownAudioProcessor::effectiveQuantizeBpm() const
{
    switch (bpmSource)
    {
        case BpmSource::file:     return fileBpm > 0.0 ? fileBpm : quantizeBpm;
        case BpmSource::host:     { const auto bpm = hostBpm.load(); return bpm > 0.0 ? bpm : quantizeBpm; }
        case BpmSource::selected: break;
    }

    return quantizeBpm;
}

void HackBrownAudioProcessor::processUploadedLoop(const juce::File& file)
{
    auto reader = createReaderForFile (file);
    inputProcessor.currFileName = file.getFileName();
    fileBpm = bpmFromFileName (file.getFileNameWithoutExtension());
    
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

    blockTimingLog.start (sampleRate, samplesPerBlock);

    // Hands classifyHit its sample rate and takes every buffer it needs, so the
    // per-hit work it does on the audio thread never reaches the allocator.
    inputProcessor.prepare (sampleRate);

    // Allocated here, on the message thread, so recordAudio never has to.
    capturedInput.setSize(renderChannels, juce::jmax(1, (int)(maxCapturedInputSeconds * sampleRate)));
    capturedInput.clear();
    analysisInput.setSize(1, capturedInput.getNumSamples());
    analysisInput.clear();
    capturedInputLength.store(0);
    wasRecording = false;

    // A take restored from the plugin's state has been waiting for the buffer
    // above to exist. Done before the defaults below so the rest of
    // prepareToPlay sees the real length.
    applyRestoredInput();

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
    blockTimingLog.stop();

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

    // A hit the classifier could not place is rendered with the hat sample, so
    // the hat's own box governs it. It used to be replaced whatever the boxes
    // said, which with only one drum ticked meant unknown hits were still cut
    // out of the recording and stood in for - a swap the user never asked for,
    // heard as a click wherever one of them sat next to a kept hit.
    return replaceHat;
}

namespace
{
    /** A buffer's length with its silent tail taken off.

        Only samples at or below the floor are dropped, so nothing audible is
        cut: the padding is buffer that was cleared and never written to, and
        every hit that reaches the end is ramped to zero before it.
    */
    /** applyGainRamp on one channel at a time would leave the other side of a
        stereo hit unramped, so every ramp in the render goes through here.
    */
    void rampAllChannels (juce::AudioBuffer<float>& buffer, int start, int length,
                          float startGain, float endGain)
    {
        if (length <= 0)
            return;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.applyGainRamp (channel, start, length, startGain, endGain);
    }

    /** Silences a span across every channel. */
    void clearAllChannels (juce::AudioBuffer<float>& buffer, int start, int length)
    {
        if (length <= 0)
            return;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.clear (channel, start, length);
    }

    int lengthWithoutSilentTail (const juce::AudioBuffer<float>& buffer)
    {
        constexpr float silenceFloor = 1.0e-6f;   // -120 dBFS

        int length = buffer.getNumSamples();

        while (length > 0)
        {
            bool silent = true;

            for (int channel = 0; channel < buffer.getNumChannels() && silent; ++channel)
                silent = std::abs (buffer.getSample (channel, length - 1)) <= silenceFloor;

            if (! silent)
                break;

            --length;
        }

        return length;
    }
}

juce::AudioBuffer<float> HackBrownAudioProcessor::renderDrumLoopOffline(
    const std::vector<DrumEventAbs>& events,
    double sampleRate,
    int outputNumSamples)
{
    juce::AudioBuffer<float> out;
    out.setSize(renderChannels, outputNumSamples);
    out.clear();

    // Hits kept at their original audio render here, apart from the samples, so
    // the normalisation below can measure and scale the samples alone. The
    // originals may already sit at full scale; running them through the same
    // gain would push them into clipping.
    juce::AudioBuffer<float> kept;
    kept.setSize(renderChannels, outputNumSamples);
    kept.clear();

    // The output opens on whichever event plays first, so everything in the
    // recording ahead of it is dropped. Found by position rather than by order:
    // nothing guarantees the events arrive sorted.
    size_t firstEvent = 0;

    for (size_t e = 1; e < events.size(); ++e)
        if (events[e].sampleIndex < events[firstEvent].sampleIndex)
            firstEvent = e;

    int offset = 0;
    int leadIn = 0;

    if (! events.empty())
    {
        offset = (int) (events[firstEvent].sampleIndex * playbackSpeed);

        // Kept at its own audio, the first hit needs the recording just ahead
        // of its onset too - that is where its attack actually starts. See
        // startLeadInSeconds.
        if (! shouldReplaceNote (events[firstEvent].midiNote))
            leadIn = juce::jmin (offset, (int) (startLeadInSeconds * sampleRate));

        offset -= leadIn;
    }

    // Where every event lands in the output, worked out up front: a hit kept
    // at its own audio has to know whether a replaced hit follows it closely
    // enough to be run into, and the events are not guaranteed to be sorted.
    std::vector<int> eventStart (events.size());

    for (size_t e = 0; e < events.size(); ++e)
        eventStart[e] = (int) (events[e].sampleIndex * playbackSpeed) - offset;

    const int fadeInSamples          = (int) (unreplacedHitFadeInSeconds  * sampleRate);
    const int keptFadeOutSamples     = (int) (unreplacedHitFadeOutSeconds * sampleRate);
    const int replacedFadeOutSamples = (int) (replacedHitFadeOutSeconds   * sampleRate);
    const int stitchSamples          = (int) (keptHitStitchSeconds        * sampleRate);

    const int capturedLength = juce::jmin (capturedInputLength.load(), capturedInput.getNumSamples());

    // With all three drums replaced the loop is pure samples and there is
    // nothing of the recording worth keeping. Switch one off, though, and what
    // was asked for is the loop as it was played with some of its hits swapped
    // out - so the recording goes down whole and the replaced hits are cut out
    // of it, rather than the kept hits being reassembled against silence.
    const bool replacingEverything = replaceKick && replaceSnare && replaceHat;

    // The bed only lines up while the hits sit where they were played. Once the
    // grid or the stretch has moved them it would run against them, so those
    // renders keep splicing hit by hit.
    const bool timingUnmodified = ! quantizeEnabled && std::abs (playbackSpeed - 1.0f) < 1.0e-6f;
    const bool useBed = ! replacingEverything && timingUnmodified && capturedLength > offset;

    /** The source span a kept event covers in the original recording, or an
        empty span for anything else. storedHits is indexed by the event's
        hitIndex and its onset is the unquantised position, which is what
        capturedInput is indexed by.
    */
    const auto keptSpan = [this, capturedLength] (const DrumEventAbs& e, int& onset, int& length)
    {
        onset = length = 0;

        if (shouldReplaceNote (e.midiNote) || e.hitIndex < 0 || e.hitIndex >= inputProcessor.storedHitsIndex)
            return false;

        const auto& hit = inputProcessor.storedHits[(size_t) e.hitIndex];

        if (hit.buffer.getNumChannels() == 0)
            return false;

        onset = hit.onsetSample;
        length = juce::jmin (hit.hitLength, hit.buffer.getNumSamples());

        return length > 0 && capturedLength > 0;
    };

    /** The event that plays next, by position rather than by order. */
    const auto nextEvent = [&eventStart] (size_t e)
    {
        int best = -1;

        for (size_t n = 0; n < eventStart.size(); ++n)
            if (eventStart[n] > eventStart[e] && (best < 0 || eventStart[n] < eventStart[(size_t) best]))
                best = (int) n;

        return best;
    };

    /** Whether a kept hit runs straight into the kept hit after it.

        Both have to be kept - a replaced hit between them has its own
        transient to protect - and they have to be within the stitch window of
        each other in the recording. The output gap is checked against the
        source gap as well: once the grid or the stretch has moved a hit, the
        recording no longer has the right number of samples to bridge it with,
        so those hits are faded as before.

        `lengthOverride` passes the length this hit is actually being played
        at, which may have been cut short; -1 asks for its own full length.
    */
    const auto stitchesForward = [&] (size_t e, int lengthOverride)
    {
        int onsetA = 0, lengthA = 0;

        if (! keptSpan (events[e], onsetA, lengthA))
            return false;

        if (lengthOverride >= 0)
            lengthA = lengthOverride;

        const int j = nextEvent (e);

        if (j < 0)
            return false;

        int onsetB = 0, lengthB = 0;

        if (! keptSpan (events[(size_t) j], onsetB, lengthB))
            return false;

        const int sourceGap = onsetB - (onsetA + lengthA);
        const int outputGap = eventStart[(size_t) j] - (eventStart[e] + lengthA);

        return sourceGap >= 0 && sourceGap <= stitchSamples && sourceGap == outputGap;
    };

    if (useBed)
    {
        const int available = juce::jmin (outputNumSamples, capturedLength - offset);

        if (available > 0)
            for (int channel = 0; channel < kept.getNumChannels(); ++channel)
                kept.copyFrom (channel, 0, capturedInput,
                               juce::jmin (channel, capturedInput.getNumChannels() - 1),
                               offset, available);

        // The lead-in can start mid-signal - it is only as long as it is
        // allowed to be, not as long as the quiet before the take - so it is
        // ramped up. The hit's own attack begins after it and is untouched.
        if (leadIn > 0)
            rampAllChannels (kept, 0, juce::jmin (fadeInSamples, leadIn), 0.0f, 1.0f);

        // Every stretch of the bed a replaced hit stands in for, in the order
        // they play, so that each one's ramps can see what follows it.
        struct Span { int start, end; };
        std::vector<Span> spans;

        for (size_t e = 0; e < events.size(); ++e)
        {
            if (! shouldReplaceNote (events[e].midiNote))
                continue;

            const int hitIndex = events[e].hitIndex;

            if (hitIndex < 0 || hitIndex >= inputProcessor.storedHitsIndex)
                continue;

            const auto& hit = inputProcessor.storedHits[(size_t) hitIndex];
            const int length = juce::jmax (0, juce::jmin (hit.hitLength, hit.buffer.getNumSamples()));

            const int start = juce::jlimit (0, outputNumSamples, eventStart[e]);
            const int end = juce::jlimit (start, outputNumSamples, eventStart[e] + length);

            if (end > start)
                spans.push_back ({ start, end });
        }

        std::sort (spans.begin(), spans.end(),
                   [] (const Span& a, const Span& b) { return a.start < b.start; });

        // Cut all of them out first: two hits close together would otherwise
        // have one's ramp written over the other's silence.
        for (const auto& span : spans)
            clearAllChannels (kept, span.start, span.end - span.start);

        for (size_t e = 0; e < spans.size(); ++e)
        {
            // Down before the hit, so the sample's transient arrives against
            // silence rather than against the recording of the hit it replaces.
            const int fadeOut = juce::jmin (keptFadeOutSamples, spans[e].start);

            rampAllChannels (kept, spans[e].start - fadeOut, fadeOut, 1.0f, 0.0f);

            const int end = spans[e].end;
            const int nextStart = e + 1 < spans.size() ? spans[e + 1].start : outputNumSamples;

            // ...and back up afterwards, unless the next replaced hit lands
            // inside the ramp, in which case the bed stays down until it.
            if (nextStart - end < fadeInSamples)
            {
                if (nextStart > end)
                    clearAllChannels (kept, end, nextStart - end);
            }
            else
            {
                rampAllChannels (kept, end, juce::jmin (fadeInSamples, outputNumSamples - end), 0.0f, 1.0f);
            }
        }

        // The render is only as long as the hits need, so a recording that runs
        // on past the last one is cut off mid-signal. Ramp that edge rather
        // than let the loop end on a step. Applied last, so no span's ramp can
        // be written over it.
        if (available >= outputNumSamples && capturedLength - offset > outputNumSamples)
            rampAllChannels (kept, outputNumSamples - keptFadeOutSamples, keptFadeOutSamples, 1.0f, 0.0f);
    }

    for (int i = 0; i < events.size(); i++) {
        //DBG("num events" << events.size());
        bool skipFilter = true;

        juce::AudioBuffer<float> copier;
        const bool replaced = shouldReplaceNote (events[i].midiNote);

        // How much of the recording ahead of this hit's onset the copy carries,
        // which only the hit that opens the loop has any.
        int lead = 0;

        if (! replaced)
        {
            // Already there, in the bed, along with everything around it.
            if (useBed)
                continue;

            // This drum is switched off: keep the hit's own audio in the loop.
            // It is spliced in at full level - it is already at its recorded
            // one - with a short fade at each end, because a hard cut into and
            // out of the rendered material would click.
            const int hitIndex = events[i].hitIndex;

            if (hitIndex < 0 || hitIndex >= inputProcessor.storedHitsIndex)
                continue;

            const auto& hit = inputProcessor.storedHits[(size_t) hitIndex];
            int hitLength = juce::jmin (hit.hitLength, hit.buffer.getNumSamples());

            if (hitLength <= 0 || hit.buffer.getNumChannels() == 0)
                continue;

            // A replaced hit must meet its sample's transient cleanly, so kept
            // audio running up to one is cut short and its fade-out brought
            // forward to finish exactly where the sample begins.
            int nextReplaced = outputNumSamples;

            for (size_t e = 0; e < events.size(); ++e)
                if (shouldReplaceNote (events[e].midiNote) && eventStart[e] > eventStart[(size_t) i])
                    nextReplaced = juce::jmin (nextReplaced, eventStart[e]);

            hitLength = juce::jmin (hitLength, nextReplaced - eventStart[(size_t) i]);

            if (hitLength <= 0)
                continue;

            // Whether this hit runs straight into the next one, and whether the
            // previous one ran into it. Only the event that comes next counts:
            // a replaced hit in between has already cut this one short above.
            const bool stitchToNext = stitchesForward ((size_t) i, hitLength);
            bool stitchFromPrevious = false;

            for (size_t e = 0; e < events.size() && ! stitchFromPrevious; ++e)
                if (e != (size_t) i)
                    stitchFromPrevious = stitchesForward (e, -1) && nextEvent (e) == (int) i;

            // Stitched forward, the hit is extended to the next one's onset with
            // the recording's own audio, so the two play as one block.
            const int tail = stitchToNext ? eventStart[(size_t) nextEvent ((size_t) i)]
                                            - (eventStart[(size_t) i] + hitLength)
                                          : 0;

            // storedHits keeps a mono copy for the classifier. capturedInput
            // holds the same audio with its channels intact and is indexed by
            // the same onset, so the hit is taken from there where it reaches,
            // and only falls back to the mono copy when it does not.
            const int from = hit.onsetSample;
            const bool fromRecording = from >= 0 && capturedLength >= from + hitLength;

            // Only the recording has anything before the onset; the stored copy
            // starts there, so a fallback has no lead-in to give.
            if (fromRecording && (size_t) i == firstEvent)
                lead = juce::jmin (leadIn, from);

            copier.setSize (renderChannels, lead + hitLength + tail);
            copier.clear();

            for (int channel = 0; channel < copier.getNumChannels(); ++channel)
            {
                if (fromRecording)
                    copier.copyFrom (channel, 0, capturedInput,
                                     juce::jmin (channel, capturedInput.getNumChannels() - 1),
                                     from - lead, lead + hitLength);
                else
                    copier.copyFrom (channel, 0, hit.buffer, 0, 0, hitLength);
            }

            if (tail > 0)
            {
                const int tailFrom = hit.onsetSample + hitLength;
                const int available = juce::jmax (0, juce::jmin (tail, capturedLength - tailFrom));

                if (available > 0)
                    for (int channel = 0; channel < copier.getNumChannels(); ++channel)
                        copier.copyFrom (channel, lead + hitLength, capturedInput,
                                         juce::jmin (channel, capturedInput.getNumChannels() - 1),
                                         tailFrom, available);
            }

            const int total = copier.getNumSamples();

            if (! stitchFromPrevious)
                rampAllChannels (copier, 0, juce::jmin (total / 2, fadeInSamples), 0.0f, 1.0f);

            if (! stitchToNext)
            {
                const int fadeOut = juce::jmin (total / 2, keptFadeOutSamples);
                rampAllChannels (copier, total - fadeOut, fadeOut, 1.0f, 0.0f);
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

            // Whatever the sample is - mono or stereo - is carried through as
            // it is; a mono sample is spread over both sides at the write.
            copier = *sound->getAudioData();
            copier.applyGain (events[i].velocity01);
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
        // The lead-in sits in front of the onset, so a copy that carries one
        // is written that much earlier - which, for the hit that opens the
        // loop, puts it at the very start of the output.
        const int startSample = eventStart[(size_t) i] - lead;

        // outputNumSamples is derived from unscaled onsets, so a playbackSpeed
        // above 1 can push a hit past the end of the buffer. Clamp the copy
        // rather than running off it.
        const int numToCopy = juce::jmin (copier.getNumSamples(), outputNumSamples - startSample);

        if (startSample < 0 || numToCopy <= 0)
            continue;

        // The sample's tail is ramped down over the part that is actually
        // written, so a sample cut off by the end of the loop still lands
        // softly. The head is left alone: that is the transient.
        if (replaced)
        {
            const int fadeOut = juce::jmin (numToCopy / 2, replacedFadeOutSamples);
            rampAllChannels (copier, numToCopy - fadeOut, fadeOut, 1.0f, 0.0f);
        }

        auto& target = replaced ? out : kept;

        for (int channel = 0; channel < target.getNumChannels(); ++channel)
        {
            auto* writePtr = target.getWritePointer(channel);
            auto* readPtr = copier.getReadPointer(juce::jmin(channel, copier.getNumChannels() - 1));

            for (int n = 0; n < numToCopy; n++) {
                writePtr[startSample + n] += readPtr[n];
            }
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
        const float peak = out.getMagnitude(0, outputNumSamples);

        if (peak > 0.0f)
            out.applyGain(1.0f / peak);

        for (int channel = 0; channel < out.getNumChannels(); ++channel)
            out.addFrom(channel, 0, kept, juce::jmin(channel, kept.getNumChannels() - 1),
                        0, outputNumSamples);
    }

    // buildDrumBuffer sizes the render with room for the longest sample loaded
    // to ring out, and nothing fills that room when the last hit is short. The
    // loop ends where its audio does, so neither the preview nor a saved file
    // carries seconds of silence on the end.
    const int length = lengthWithoutSilentTail (out);

    if (length < out.getNumSamples())
        out.setSize (out.getNumChannels(), length, true, true, true);

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
            
            //logFile << odfValue << std::endl;
            //DBG("odf: " << odfValue);
            
            if (onsetConfirmedThisSample) {
                int compensatedOnset = currSampleInFile;
                //DBG("Onset detected at sample index: " << compensatedOnset);
            }
            
            samplesAccumulated = 0;
        }

        // Process every sample (including the very first ones) into the InputProcessor
        float amp = envelopeFollower.processSample (0, sample);
        inputProcessor.processSample (sample, amp, onsetConfirmedThisSample);
    }
}

void HackBrownAudioProcessor::triggerNewlyClassifiedHits (bool play)
{
    const auto& hits = inputProcessor.classifiedHits;
    const int total = (int) hits.size();

    // reanalyseCapturedInput rebuilds classifiedHits from scratch, so the count
    // can fall as well as rise; without this the counter would sit past the end
    // and never fire again.
    if (total < liveHitsPlayed)
        liveHitsPlayed = total;

    for (; liveHitsPlayed < total; ++liveHitsPlayed)
    {
        const auto& hit = hits[(size_t) liveHitsPlayed];

        // rms is a bare RMS with no reference, so it sits well below full scale.
        // The offline render deals with that by normalising the finished loop,
        // and nothing can normalise a stream that is still arriving - so the
        // level is taken against the loudest hit of the take so far instead.
        // The floor keeps the first few hits of a quiet take audible.
        livePeakRms = juce::jmax (livePeakRms, hit.rms);

        if (! play)
            continue;

        // A drum the user has switched off keeps its own audio in the rendered
        // loop, so there is no sample to monitor it with either.
        if (! shouldReplaceNote ((int) hit.type))
            continue;

        // HitType doubles as the MIDI note. Anything the classifier could not
        // place has no sample of its own and falls back to the hat, which is
        // what the offline render does with it. The note goes straight to
        // drumSynth rather than through drumSoundsByNote, because the synth
        // guards its own sound list against the message thread swapping a
        // sample in and that map does not.
        const auto note = (int) (hit.type == HitType::Unknown ? HitType::Hat : hit.type);

        const float velocity = livePeakRms > 0.0f
                             ? juce::jlimit (0.2f, 1.0f, hit.rms / livePeakRms)
                             : 1.0f;

        drumSynth.noteOn (1, note, velocity);
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
            // A mono input is carried on both sides rather than left half
            // silent; the analysis still reads channel 0 either way.
            for (int channel = 0; channel < capturedInput.getNumChannels(); ++channel)
                capturedInput.copyFrom(channel, written, buffer,
                                       juce::jmin(channel, totalNumInputChannels - 1), 0, toKeep);

            // What the detector gets is channel 0, exactly as it was before the
            // capture kept both sides.
            analysisInput.copyFrom(0, written, buffer, 0, 0, toKeep);

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

    const bool livePlay = livePlayEnabled.load();

    // Classification runs inline in classifyAudioBlock above, so a hit that
    // closed anywhere in this block is already in classifiedHits and goes out
    // in this same block. That is as close behind the player as the analysis
    // allows: a hit is only classified once it has ended, so the sample lands
    // roughly a hit-length after the onset that caused it.
    triggerNewlyClassifiedHits (livePlay);

    // The take itself is never passed through - the input is captured, not
    // monitored - so the buffer is cleared before the samples are mixed in.
    buffer.clear();

    if (livePlay)
        drumSynth.renderNextBlock (buffer, liveNoMidi, 0, buffer.getNumSamples());
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

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        // Stereo out of a stereo source; a mono source still reaches every
        // output, as it did when channel 0 was fanned out to all of them.
        auto* inputData = source.getReadPointer(juce::jmin(ch, source.getNumChannels() - 1));

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

        inputPreviewBuffer.setSize(capturedInput.getNumChannels(), juce::jmax(0, length));

        if (length > 0)
            for (int channel = 0; channel < inputPreviewBuffer.getNumChannels(); ++channel)
                inputPreviewBuffer.copyFrom(channel, 0, capturedInput, channel, 0, length);
    }

    previewingInput.store(source == PreviewSource::input);
    renderedReadPos = 0;
    isPlaybackOn.store(true);
}

void HackBrownAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // First thing in, last thing out, so the early returns below are timed too.
    BlockTimingLog::ScopedBlock blockTimer (blockTimingLog, buffer.getNumSamples());

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

    // The play head is only valid inside this call, so the tempo is copied out
    // here for the message thread to quantise against. Left as it was when the
    // host has nothing to say, so a transient gap does not drop the grid back
    // to the selected BPM.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                if (*bpm > 0.0)
                    hostBpm.store (*bpm);
    
    const bool recording = recordingEnabled.load();
    blockTimer.recording = recording;

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

        // Lets a hit that was still decaying when the take stopped finish
        // instead of being cut off mid-tail. Costs nothing once the voices are
        // idle, and the output is already silent here.
        if (livePlayEnabled.load())
            drumSynth.renderNextBlock (buffer, liveNoMidi, 0, buffer.getNumSamples());
    }
}

void HackBrownAudioProcessor::reclassifyStoredHits()
{
    // The audio thread owns the detector and the stored hits during a take.
    if (recordingEnabled.load())
        return;

    if (inputProcessor.storedHitsIndex <= 0)
        return;

    isPlaybackOn.store (false);

    // The rate the hits were captured at, not the host's current one: an
    // uploaded loop was analysed at whatever rate was in force then, and the
    // features have to be extracted the same way to mean the same thing.
    inputProcessor.classifyStoredHits (inputProcessor.analysisSampleRate);

    reconstructLoopFromHits();
}

void HackBrownAudioProcessor::reconstructLoopFromHits() {
    // algorithm classifies/do classification
    isPlaybackOn.store(false);

    // Each hit was classified by InputProcessor as it closed, so there is
    // nothing to classify here - only the loop to rebuild from the labels.
    // When the decision itself has to be made again, reclassifyStoredHits
    // runs the classifier first and then calls this.
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
namespace
{
    // Audio in the plugin's state is plain little-endian float samples behind a
    // small header - no codec, so what comes back is bit-for-bit what went in.
    void writeAudioChunk (juce::MemoryOutputStream& out, const juce::AudioBuffer<float>& buffer,
                          int numSamples, double sampleRate)
    {
        const int channels = (numSamples > 0 ? buffer.getNumChannels() : 0);
        const int samples  = (channels > 0 ? juce::jmin (numSamples, buffer.getNumSamples()) : 0);

        out.writeInt (channels > 0 ? channels : 0);
        out.writeInt (samples);
        out.writeDouble (sampleRate);

        for (int channel = 0; channel < channels; ++channel)
            out.write (buffer.getReadPointer (channel), (size_t) samples * sizeof (float));
    }

    bool readAudioChunk (juce::MemoryInputStream& in, juce::AudioBuffer<float>& buffer, double& sampleRate)
    {
        const int channels = in.readInt();
        const int samples  = in.readInt();
        sampleRate = in.readDouble();

        // A truncated or corrupt block must not be able to ask for a buffer
        // larger than the bytes that are actually there.
        if (channels <= 0 || samples <= 0 || channels > 16
            || (juce::int64) channels * samples * (juce::int64) sizeof (float) > in.getNumBytesRemaining())
        {
            buffer.setSize (0, 0);
            return false;
        }

        buffer.setSize (channels, samples);

        for (int channel = 0; channel < channels; ++channel)
            in.read (buffer.getWritePointer (channel), (int) ((size_t) samples * sizeof (float)));

        return true;
    }

    /** One drum sample, written as a float WAV so it reloads through the same
        reader path a file from disk takes.
    */
    void writeSample (juce::MemoryOutputStream& out, const DrumSamplerSound::Ptr& sound)
    {
        juce::MemoryBlock encoded;

        if (sound != nullptr && sound->getAudioData() != nullptr
            && sound->getAudioData()->getNumSamples() > 0)
        {
            const auto& audio = *sound->getAudioData();

            // Scoped: the writer only flushes into the block as it is destroyed.
            {
                auto stream = std::unique_ptr<juce::OutputStream> (new juce::MemoryOutputStream (encoded, false));

                juce::WavAudioFormat wav;

                const auto options = juce::AudioFormatWriterOptions{}
                                        .withSampleRate (sound->getSourceSampleRate())
                                        .withNumChannels (audio.getNumChannels())
                                        .withBitsPerSample (32)
                                        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

                if (auto writer = wav.createWriterFor (stream, options))
                    writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
        }

        out.writeString (sound != nullptr ? sound->getName() : juce::String());
        out.writeInt ((int) encoded.getSize());
        out.write (encoded.getData(), encoded.getSize());
    }
}

void HackBrownAudioProcessor::applyRestoredInput()
{
    const int length = restoredInput.getNumSamples();

    if (length <= 0 || restoredInput.getNumChannels() <= 0 || capturedInput.getNumSamples() <= 0)
        return;

    const int toCopy = juce::jmin (length, capturedInput.getNumSamples());

    capturedInput.clear();

    for (int channel = 0; channel < capturedInput.getNumChannels(); ++channel)
        capturedInput.copyFrom (channel, 0, restoredInput,
                                juce::jmin (channel, restoredInput.getNumChannels() - 1), 0, toCopy);

    // Rebuilt by averaging, which is what an uploaded loop was analysed as and
    // what a mono take amounts to. A take recorded from a genuinely stereo
    // input and then restored is the one case this does not reproduce exactly.
    analysisInput.clear (0, 0, juce::jmin (toCopy, analysisInput.getNumSamples()));

    for (int channel = 0; channel < restoredInput.getNumChannels(); ++channel)
        analysisInput.addFrom (0, 0, restoredInput, channel, 0,
                               juce::jmin (toCopy, analysisInput.getNumSamples()),
                               1.0f / (float) restoredInput.getNumChannels());
    capturedInputLength.store (toCopy);

    restoredInput.setSize (0, 0);
}

void HackBrownAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream out (destData, false);

    out.writeInt (stateMagic);
    out.writeInt (stateVersion);

    // --- what the settings page drives ---
    out.writeBool (replaceKick);
    out.writeBool (replaceSnare);
    out.writeBool (replaceHat);
    out.writeFloat (playbackSpeed);
    out.writeBool (quantizeEnabled);
    out.writeInt ((int) bpmSource);
    out.writeDouble (quantizeBpm);
    out.writeDouble (quantizeDivision);
    out.writeDouble (swingAmount);
    out.writeDouble (fileBpm);
    out.writeFloat (sensitivity.load());
    out.writeDouble (inputProcessor.classBias.kick);
    out.writeDouble (inputProcessor.classBias.snare);
    out.writeDouble (inputProcessor.classBias.hat);

    // --- the editor's own state, so the window reopens where it was left ---
    out.writeInt (editorState.openPage);
    out.writeFloat (editorState.aboutScroll);

    out.writeInt ((int) editorState.checkedBoxes.size());

    for (const auto box : editorState.checkedBoxes)
        out.writeByte (box);

    out.writeInt ((int) editorState.sliderValues.size());

    for (const auto value : editorState.sliderValues)
        out.writeFloat (value);

    // --- the loaded drum samples ---
    for (const auto drum : { kick, snare, hat })
        writeSample (out, getSoundForNote (drumMidiMap[drum]));

    // --- the take or uploaded loop, exactly as it came in ---
    writeAudioChunk (out, capturedInput, capturedInputLength.load(), currentSampleRate);

    // --- and the loop Drumify built from it ---
    writeAudioChunk (out, renderedDrumBuffer, renderedDrumBuffer.getNumSamples(), currentSampleRate);

    // --- the analysis, so the loop can be rebuilt when a setting changes ---
    out.writeInt ((int) inputProcessor.classifiedHits.size());

    for (const auto& hit : inputProcessor.classifiedHits)
    {
        out.writeInt (hit.hitIndex);
        out.writeInt (hit.onsetSample);
        out.writeInt ((int) hit.type);
        out.writeFloat (hit.rms);
        out.writeFloat (hit.zcr);
        out.writeFloat (hit.durationSec);
    }

    // The audio of each detected hit, which an unreplaced drum plays instead
    // of a sample. Only the part of each buffer that was actually filled.
    const int numStored = juce::jlimit (0, InputProcessor::numHits, inputProcessor.storedHitsIndex);

    out.writeInt (numStored);

    for (int i = 0; i < numStored; ++i)
    {
        const auto& hit = inputProcessor.storedHits[(size_t) i];
        const int length = hit.buffer.getNumChannels() > 0
                             ? juce::jmax (0, juce::jmin (hit.hitLength, hit.buffer.getNumSamples()))
                             : 0;

        out.writeInt (hit.onsetSample);
        out.writeInt (length);
        out.writeInt ((int) hit.type);

        if (length > 0)
            out.write (hit.buffer.getReadPointer (0), (size_t) length * sizeof (float));
    }
}

void HackBrownAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 8)
        return;

    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);

    if (in.readInt() != stateMagic || in.readInt() != stateVersion)
        return;   // written by another build: leave the defaults alone

    // The buffers below are swapped out from under playAudio, so stop it first.
    isPlaybackOn.store (false);

    replaceKick  = in.readBool();
    replaceSnare = in.readBool();
    replaceHat   = in.readBool();
    playbackSpeed = in.readFloat();
    quantizeEnabled = in.readBool();
    bpmSource = (BpmSource) juce::jlimit (0, 2, in.readInt());
    quantizeBpm = in.readDouble();
    quantizeDivision = in.readDouble();
    swingAmount = in.readDouble();
    fileBpm = in.readDouble();
    sensitivity.store (in.readFloat());
    inputProcessor.classBias.kick  = in.readDouble();
    inputProcessor.classBias.snare = in.readDouble();
    inputProcessor.classBias.hat   = in.readDouble();

    applySensitivity();

    editorState.openPage = in.readInt();
    editorState.aboutScroll = in.readFloat();

    const int numChecks = juce::jlimit (0, 64, in.readInt());
    editorState.checkedBoxes.clear();

    for (int i = 0; i < numChecks; ++i)
        editorState.checkedBoxes.push_back ((char) in.readByte());

    const int numSliders = juce::jlimit (0, 64, in.readInt());
    editorState.sliderValues.clear();

    for (int i = 0; i < numSliders; ++i)
        editorState.sliderValues.push_back (in.readFloat());

    for (const auto drum : { kick, snare, hat })
    {
        const auto name = in.readString();
        const int encodedSize = in.readInt();

        if (encodedSize <= 0 || (juce::int64) encodedSize > in.getNumBytesRemaining())
            continue;   // nothing was saved for this drum: it keeps its default

        juce::MemoryBlock encoded ((size_t) encodedSize);
        in.read (encoded.getData(), encodedSize);

        juce::WavAudioFormat wav;

        // The stream keeps its own copy, so it outlives this block.
        if (auto* reader = wav.createReaderFor (new juce::MemoryInputStream (encoded, true), true))
            loadSampleFromReader (std::unique_ptr<juce::AudioFormatReader> (reader), name, drumMidiMap[drum]);
    }

    getLongestSampleLengthInSamples();

    // capturedInput is only sized once prepareToPlay has run, which may be
    // after this, so the take waits in restoredInput until it has.
    double takeRate = 0.0;
    readAudioChunk (in, restoredInput, takeRate);
    applyRestoredInput();

    double loopRate = 0.0;
    readAudioChunk (in, renderedDrumBuffer, loopRate);
    renderedReadPos = 0;

    inputProcessor.classifiedHits.clear();

    const int numHits = juce::jlimit (0, InputProcessor::numHits, in.readInt());

    for (int i = 0; i < numHits; ++i)
    {
        ClassifiedHit hit;
        hit.hitIndex = in.readInt();
        hit.onsetSample = in.readInt();
        hit.type = (HitType) in.readInt();
        hit.rms = in.readFloat();
        hit.zcr = in.readFloat();
        hit.durationSec = in.readFloat();

        inputProcessor.classifiedHits.push_back (hit);
    }

    const int numStored = juce::jlimit (0, InputProcessor::numHits, in.readInt());

    inputProcessor.storedHitsIndex = 0;

    for (int i = 0; i < numStored; ++i)
    {
        auto& hit = inputProcessor.storedHits[(size_t) i];

        hit.onsetSample = in.readInt();
        const int length = in.readInt();
        hit.type = (HitType) in.readInt();

        if (length > 0 && (juce::int64) length * (juce::int64) sizeof (float) <= in.getNumBytesRemaining())
        {
            hit.buffer.setSize (1, length);
            in.read (hit.buffer.getWritePointer (0), (int) ((size_t) length * sizeof (float)));
            hit.hitLength = length;
        }
        else
        {
            hit.buffer.setSize (1, 0);
            hit.hitLength = 0;
        }

        ++inputProcessor.storedHitsIndex;
    }
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

    if (quantizeEnabled && ! timed.empty()) {
        DBG("Quantising!");

        // The grid is anchored on the first hit of the take, not on sample
        // zero. Nobody starts playing exactly on a beat, and a grid measured
        // from the start of the recording would move every hit in it by that
        // same opening gap - the whole take sitting consistently off the grid
        // rather than on it. Anchored here, the first hit stays where it was
        // played and everything else is snapped relative to it.
        const int anchor = std::min_element (timed.begin(), timed.end(),
                                             [] (const ClassifiedHit& a, const ClassifiedHit& b)
                                             { return a.onsetSample < b.onsetSample; })->onsetSample;

        int quantizeUnitInSamples = (60.0f / effectiveQuantizeBpm()) * quantizeDivision * 4 * currentSampleRate;
        int maxSwingUnit = (static_cast<int>(((1.0f / 16.0f) / quantizeDivision)) * quantizeUnitInSamples) >> 1;
        maxSwingUnit *= 0.8;
        
        for (auto& hit : timed) {
            int relativeStartSample = hit.onsetSample - anchor;
            int unitIndex = std::floor(static_cast<float>(relativeStartSample) / static_cast<float>(quantizeUnitInSamples));
            int startingSample = unitIndex * quantizeUnitInSamples;
            int endingSample = startingSample + quantizeUnitInSamples;
            
            if (swingAmount > 0) {
                int numSamplesSwing = maxSwingUnit * swingAmount;
                
                if (unitIndex & 1) startingSample += numSamplesSwing;
                else endingSample += numSamplesSwing;
            }
            
            if (std::abs(relativeStartSample - startingSample) > std::abs(relativeStartSample - endingSample)) startingSample = endingSample;
            hit.onsetSample = startingSample + anchor;
        }
    }

    // Hits within minHitSpacingSeconds of each other collapse into the earliest
    // of them, quantised or not: the input is one voice, so two that close are
    // either one hit the detector split or, on the grid, two folded onto the
    // same slot. Each hit is measured against the last one kept, not the last
    // one seen, so a cluster thins to a spaced set rather than disappearing
    // altogether. The sort is stable so that hits the grid made simultaneous
    // keep their detection order, and the earlier wins. hitIndex still points
    // into storedHits, which is not compacted, so the surviving hits keep
    // theirs as they are.
    std::stable_sort (timed.begin(), timed.end(),
                      [] (const ClassifiedHit& a, const ClassifiedHit& b) { return a.onsetSample < b.onsetSample; });

    const int minSpacing = juce::roundToInt (minHitSpacingSeconds * currentSampleRate);
    std::vector<ClassifiedHit> kept;
    kept.reserve (timed.size());

    for (const auto& hit : timed)
        if (kept.empty() || hit.onsetSample - kept.back().onsetSample >= minSpacing)
            kept.push_back (hit);

    return kept;
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
