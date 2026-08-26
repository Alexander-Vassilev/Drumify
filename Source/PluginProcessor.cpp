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
    // ... other resets ...
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

    auto* sound = new juce::SamplerSound (sampleName,
                                          *reader,
                                          noteRange,
                                          midiNote,
                                          attack,
                                          release,
                                          10.0);

    // Clear old sounds assigned to this midiNote so they don't stack up
    for (int i = drumSynth.getNumSounds() - 1; i >= 0; --i)
    {
        if (auto* oldSound = dynamic_cast<juce::SamplerSound*> (drumSynth.getSound(i).get()))
        {
            if (oldSound->appliesToNote (midiNote))
                drumSynth.removeSound (i);
        }
    }

    drumSynth.addSound (sound);
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
    
    const float* totalInputData = loopBuffer.getReadPointer (0);

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
    drumSynth.clearSounds();
    
    drumMidiMap[kick]  = 36;
    drumMidiMap[snare] = 38;
    drumMidiMap[hat]   = 42;

    loadSampleFromBinaryData("Kick", BinaryData::Kick_wav, BinaryData::Kick_wavSize, drumMidiMap[kick]);
    loadSampleFromBinaryData("Snare", BinaryData::Snare_wav, BinaryData::Snare_wavSize, drumMidiMap[snare]);
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

juce::AudioBuffer<float> HackBrownAudioProcessor::renderDrumLoopOffline(
    const std::vector<DrumEventAbs>& events,
    double sampleRate,
    int outputNumSamples)
{
    juce::AudioBuffer<float> out;
    out.setSize(2, outputNumSamples);
    out.clear();

    juce::SynthesiserSound::Ptr kick = drumSynth.getSound(0);
    //DBG("a");
    auto* samplerSound = dynamic_cast<juce::SamplerSound*>(kick.get());
    //DBG("b");
    juce::AudioBuffer<float>* kickData = samplerSound->getAudioData();
    //DBG("c");
    int kickLen = kickData->getNumSamples();

    //DBG("built ma kick with a length of: " << kickLen);
    
    

    juce::SynthesiserSound::Ptr snare = drumSynth.getSound(1);
    samplerSound = dynamic_cast<juce::SamplerSound*>(snare.get());
    juce::AudioBuffer<float>* snareData = samplerSound->getAudioData();
    int snareLen = snareData->getNumSamples();

    //DBG("built ma snare");

    juce::SynthesiserSound::Ptr hat = drumSynth.getSound(2);
    samplerSound = dynamic_cast<juce::SamplerSound*>(hat.get());
    juce::AudioBuffer<float>* hatData = samplerSound->getAudioData();
    int hatLen = hatData->getNumSamples();
    int offset = 0;

    if (events.size() >= 1) {
        offset = events[0].sampleIndex * playbackSpeed;
    }
    
    for (int i = 0; i < events.size(); i++) {
        //DBG("num events" << events.size());
        juce::AudioBuffer<float> copier;
        bool skipFilter = true;

        switch (events[i].midiNote) {
        case 36:
            copier = *kickData;
            break;
        case 38:
            copier = *snareData;
            break;
        case 42:
            copier = *hatData;
            break;
        default:
            copier = *hatData;
            //skip = true;
            break;
        };

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
        out.copyFrom(0, events[i].sampleIndex * playbackSpeed - offset, copier, 0, 0, copier.getNumSamples());
        //DBG("applying gain ramp");
        out.applyGainRamp(0, events[i].sampleIndex * playbackSpeed - offset, copier.getNumSamples(), events[i].velocity01, events[i].velocity01);
        //DBG("copied");
    }

    //out.copyFrom(0, processLen, *audioData, 0, 0, processLen);
    //out.clear(0, 2 * processLen, outputNumSamples - 2 * processLen);
    //DBG("finished building buffer");

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

    //juce::AudioBuffer<float>& readBuff;

    const int numSamples = buffer.getNumSamples();
    const int remaining = renderedDrumBuffer.getNumSamples() - renderedReadPos;
    const int toCopy = juce::jmin(numSamples, remaining);

    auto* inputData = renderedDrumBuffer.getReadPointer(0);

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

    if (renderedReadPos >= renderedDrumBuffer.getNumSamples()) {
        DBG("Stop playback");
        isPlayingRendered = false;
        isPlaybackOn.store(false);
        renderedReadPos = 0;
    }

    midiMessages.clear();
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
    
    if (buffer.getNumSamples() == 0 || totalNumInputChannels == 0) {
        DBG("input channels 0");
        jassertfalse;
        return;
    }
    
    if (recordingEnabled.load()) {
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

void HackBrownAudioProcessor::buildDrumBuffer() {
    std::vector<DrumEventAbs> events;
    const double sr = currentSampleRate;
    int lastSampleHit = 0;
    float lastSize = 0;

    for (auto processedHit : inputProcessor.classifiedHits) {
        lastSampleHit = processedHit.onsetSample;
        lastSize = processedHit.durationSec;
        events.push_back({ processedHit.onsetSample, (int)processedHit.type, processedHit.rms }); // kick at 0s
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
