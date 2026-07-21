/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SineGenerator.h"
#include "InputProcessor.h"

enum DrumType {
    kick,
    snare,
    hat
};

//==============================================================================
/**
*/
class HackBrownAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    HackBrownAudioProcessor();
    ~HackBrownAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void makeTestRender();
    void buildDrumBuffer();

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    void loadSampleFromFile (const juce::File& file, int midiNote);
    std::unique_ptr<juce::AudioFormatReader> createReaderForFile (const juce::File& file);
    void processUploadedLoop(const juce::File& file);
    
    void reconstructLoopFromHits();
    void getLongestSampleLengthInSamples();
    
    std::atomic<bool> recordingEnabled { false };
    std::atomic<bool> recordingStarted { false };
    std::atomic<bool> isPlaybackOn { false };
    juce::AudioBuffer<float> renderedTestBuffer;
    InputProcessor inputProcessor;
    float playbackSpeed = 1;
    std::map<DrumType, int> drumMidiMap;
private:
    void reset();
    void analyzeLoadedDrumLoop (const juce::AudioBuffer<float>& loopBuffer);
    void classifyAudioBlock (int channel, const float* inputData, int numSamples);
    void recordAudio(juce::AudioBuffer<float>& buffer);
    void playAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    
    juce::dsp::BallisticsFilter<float> envelopeFollower;
    SineGenerator sineGenerator;

    double currentSampleRate = 44100.0;

    juce::Synthesiser drumSynth;
    int numSamplesLongestSound = 0; // Length of longest drumSynth sound in samples
    juce::AudioFormatManager formatManager;
    
    void loadSampleFromReader (std::unique_ptr<juce::AudioFormatReader> reader,
                               const juce::String& sampleName,
                               int midiNote);
    void loadSampleFromBinaryData (const juce::String& name,
                               const void* data,
                               int dataSize,
                               int midiNote);


    // Offline event type (absolute time)
    struct DrumEventAbs
    {
        int sampleIndex;   // absolute sample index in rendered timeline
        int midiNote;      // 36 kick, 38 snare, 42 hat...
        float velocity01;  // 0..1
        bool filterOn = false;     // Determines whether to apply formant-accentuating bell filter
        float centerFreq = 1000;  // Filter centre freq
    };

    // Offline render
    juce::AudioBuffer<float> renderDrumLoopOffline (const std::vector<DrumEventAbs>& events,
                                                    double sampleRate,
                                                    int outputNumSamples);

    static constexpr int fftOrder = 11;
    static constexpr int fftWindowSize = 1 << fftOrder;
    static constexpr int fftHopSize = 256;
    static constexpr int bufferMask = (1 << fftOrder) - 1;
    
    int currSampleInFile = 0;
    float fifoBuffer[fftWindowSize] = {};
    bool isFifoFilled = false;
    int fifoIndex = 0;
    int samplesAccumulated = 0;
    
    ComplexOdf complexOnsetDetector { fftOrder, currentSampleRate }; // Order 10 = size 1024
    static constexpr float statisticalRatioThreshold = 1.5f; // Adjust this threshold to taste
    static constexpr float statisticalAbsoluteThreshold = 3000.0f; // Adjust this threshold to taste
    static constexpr int baseMeanLength = 1; // Adjust this threshold to taste
    static constexpr int mediumHistoryMeanLength = 6; // Adjust this threshold to taste
    static constexpr int longHistoryMeanLength = 20; // Adjust this threshold to taste
    StatisticalOnsetDetector statisticalDetector { statisticalRatioThreshold, statisticalAbsoluteThreshold, baseMeanLength, mediumHistoryMeanLength, longHistoryMeanLength };
    
    // Rendered playback state
    juce::AudioBuffer<float> renderedDrumBuffer;
    int renderedReadPos = 0;
    bool isPlayingRendered = false;
    
    // Filter
    juce::dsp::IIR::Filter<float> bellFilter;
    
    std::ofstream logFile;
    juce::File file = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("ODFAnalysis/ODFValues_beatbox.txt");
    
    bool playprint = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HackBrownAudioProcessor)
};
