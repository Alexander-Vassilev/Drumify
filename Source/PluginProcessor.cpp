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
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
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

void HackBrownAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String HackBrownAudioProcessor::getProgramName (int index)
{
    return {};
}

void HackBrownAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
//the function that wraps BinaryData in a MemoryInputStream
void HackBrownAudioProcessor::loadSampleFromBinaryData (const juce::String& name, const void* data, int dataSize, int midiNote)
{
    auto stream = std::make_unique<juce::MemoryInputStream>(data, (size_t) dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (std::move(stream)));

    if (reader == nullptr)
        return;

    juce::BigInteger noteRange;
    noteRange.setBit (midiNote);

    const double attack  = 0.001;
    const double release = 0.05;

    auto* sound = new juce::SamplerSound (name,
                                          *reader,
                                          noteRange,
                                          midiNote,
                                          attack,
                                          release,
                                          10.0);

    drumSynth.addSound (sound);
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
void HackBrownAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    //static bool ranTests = false;
    //if (!ranTests)
    //{
    //    ranTests = true;
    //    runDrumClassifierSmokeTests();
    //}

    juce::dsp::ProcessSpec spec;

    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    //spec.numChannels = numChannels;
    envelopeFollower.prepare(spec);
    envelopeFollower.setAttackTime(15.0f);
    envelopeFollower.setReleaseTime(80.0f);
    sineGenerator.prepare(sampleRate, samplesPerBlock);
    
    drumSynth.clearVoices();
    
    for (int i = 0; i < 16; ++i)
        drumSynth.addVoice (new juce::SamplerVoice());

    drumSynth.setCurrentPlaybackSampleRate (sampleRate);

    drumSynth.clearSounds();
    
    loadSampleFromBinaryData ("Kick",  BinaryData::Kick_wav,  BinaryData::Kick_wavSize,  36);
    loadSampleFromBinaryData ("Snare", BinaryData::Snare_wav, BinaryData::Snare_wavSize, 38);
    loadSampleFromBinaryData ("Hat",   BinaryData::Hat_wav,   BinaryData::Hat_wavSize,   42);
    currentSampleRate = sampleRate;
    //makeTestRender(); //TEMP, remove it later!!
}

void HackBrownAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool HackBrownAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
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
    auto* samplerSound = dynamic_cast<juce::SamplerSound*>(kick.get());
    juce::AudioBuffer<float>* kickData = samplerSound->getAudioData();
    int kickLen = kickData->getNumSamples();
    
    DBG("built ma kick");
    
    juce::SynthesiserSound::Ptr snare = drumSynth.getSound(1);
    samplerSound = dynamic_cast<juce::SamplerSound*>(snare.get());
    juce::AudioBuffer<float>* snareData = samplerSound->getAudioData();
    int snareLen = snareData->getNumSamples();
    
    DBG("built ma snare");
    
    juce::SynthesiserSound::Ptr hat = drumSynth.getSound(2);
    samplerSound = dynamic_cast<juce::SamplerSound*>(hat.get());
    juce::AudioBuffer<float>* hatData = samplerSound->getAudioData();
    int hatLen = hatData->getNumSamples();
    
    for (DrumEventAbs event : events) {
        DBG("in da loop");
        juce::AudioBuffer<float> copier;
        bool skip = false;
        
        switch (event.midiNote) {
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
                skip = true;
                break;
        };
        
        if (!skip) {
            out.copyFrom(0, event.sampleIndex, copier, 0, 0, copier.getNumSamples());
        }
    }
    
    //out.copyFrom(0, processLen, *audioData, 0, 0, processLen);
    //out.clear(0, 2 * processLen, outputNumSamples - 2 * processLen);
    DBG("finished building buffer");
    
    return out;
    //auto* snare = drumSynth.getSound(1);
    //auto* hat = drumSynth.getSound(2);
    
    
    // Ensure synth is configured
    /*
    drumSynth.setCurrentPlaybackSampleRate(sampleRate);

    // Build a global MIDI timeline (absolute sample positions)
    juce::MidiBuffer globalMidi;
    const int noteOffDelay = int(0.05 * sampleRate); // 50ms

    for (const auto& e : events)
    {
        auto on = juce::MidiMessage::noteOn(
            1, e.midiNote,
            (juce::uint8) juce::jlimit(1, 127, int(e.velocity01 * 127.0f))
        );
        auto off = juce::MidiMessage::noteOff(1, e.midiNote);

        globalMidi.addEvent(on, e.sampleIndex);
        globalMidi.addEvent(off, e.sampleIndex + noteOffDelay);
    }

    // Render in chunks
    const int blockSize = 512;
    juce::MidiBuffer blockMidi;
    
    DBG("outputNumSamples " << outputNumSamples);
    DBG("blockSize " << blockSize);
    
    for (int pos = 0; pos < outputNumSamples; pos += blockSize)
    {
        const int numThisBlock = juce::jmin(blockSize, outputNumSamples - pos);
        blockMidi.clear();

        // Copy events that fall inside [pos, pos+numThisBlock) into blockMidi with relative offsets
        for (const auto metadata : globalMidi)
        {
            const int eventSample = metadata.samplePosition;
            if (eventSample >= pos && eventSample < pos + numThisBlock)
            {
                blockMidi.addEvent(metadata.getMessage(), eventSample - pos);
            }
        }

        for (const auto metadata : blockMidi)
        {
            auto message = metadata.getMessage();
            DBG("Sample: " << metadata.samplePosition
                << " Note: " << message.getNoteNumber()
                << " Velocity: " << message.getVelocity()
                << " Is NoteOn: " << (int)message.isNoteOn());
        }
        drumSynth.renderNextBlock(out, blockMidi, 100, numThisBlock);
    }

    return out;
     */
}


void HackBrownAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    if (recordingEnabled.load()) {
        isPlayingRendered = true;
        recordingStarted.store(true);
        juce::ScopedNoDenormals noDenormals;
        auto totalNumInputChannels  = getTotalNumInputChannels();
        auto totalNumOutputChannels = getTotalNumOutputChannels();
    
        for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, buffer.getNumSamples());


        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            juce::ignoreUnused(channelData);
        }

        auto* inputData = buffer.getReadPointer(0);

        for (int channel = 0; channel < totalNumOutputChannels; ++channel) {
            float* channelData = buffer.getWritePointer(channel);

            // Only process if we have a corresponding input channel
            if (channel < totalNumInputChannels) {
                for (int sample = 0; sample < buffer.getNumSamples(); sample++) {
                    float amp = envelopeFollower.processSample(channel, inputData[sample]);
                    inputProcessor.processSample(inputData[sample], amp);
                }
            }
            else {
                // Clear any extra output channels
                buffer.clear(channel, 0, buffer.getNumSamples());
            }
        }
        
        buffer.clear();
    } else if (isPlaybackOn.load()) {
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
            isPlayingRendered = false;
            isPlaybackOn.store(false);
            renderedReadPos = 0;
        }

        midiMessages.clear();
        return;
    } else {
        buffer.clear();
    }
}

//==============================================================================
bool HackBrownAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* HackBrownAudioProcessor::createEditor()
{
    return new HackBrownAudioProcessorEditor (*this);
}

//==============================================================================
void HackBrownAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void HackBrownAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    int lastSize = 0;
    
    for (auto processedHit : inputProcessor.classifiedHits) {
        lastSampleHit = processedHit.onsetSample;
        lastSize = processedHit.durationSec;
        events.push_back({ processedHit.onsetSample, (int)processedHit.type, 1.0f }); // kick at 0s
    }
    
    const int outLen = int(lastSampleHit + lastSize * sr + 100);
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
