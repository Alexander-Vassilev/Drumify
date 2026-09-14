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

/** A SamplerSound that remembers the drum note it was loaded for.

    juce::SamplerSound keeps its root note private and the Synthesiser only
    exposes sounds by position, which reshuffles every time a sample is replaced
    (removeSound then addSound appends to the end). Tagging each sound lets the
    offline renderer look a drum up directly instead of trusting that order.
*/
class DrumSamplerSound : public juce::SamplerSound
{
public:
    DrumSamplerSound (const juce::String& name,
                      juce::AudioFormatReader& source,
                      const juce::BigInteger& midiNotes,
                      int midiNoteForNormalPitch,
                      double attackTimeSecs,
                      double releaseTimeSecs,
                      double maxSampleLengthSeconds)
        : juce::SamplerSound (name, source, midiNotes, midiNoteForNormalPitch,
                              attackTimeSecs, releaseTimeSecs, maxSampleLengthSeconds),
          midiNote (midiNoteForNormalPitch)
    {
    }

    int getMidiNote() const noexcept { return midiNote; }

    using Ptr = juce::ReferenceCountedObjectPtr<DrumSamplerSound>;

private:
    const int midiNote;
};

/** Which audio the preview speakers play back. */
enum class PreviewSource {
    input,   // the hits captured from the mic
    output   // the reconstructed drum loop
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
    
    /** The sound currently loaded for a drum note, or nullptr if there is none. */
    DrumSamplerSound::Ptr getSoundForNote (int midiNote) const;

    void loadSampleFromFile (const juce::File& file, int midiNote);
    std::unique_ptr<juce::AudioFormatReader> createReaderForFile (const juce::File& file);
    void processUploadedLoop(const juce::File& file);
    
    void reconstructLoopFromHits();
    void getLongestSampleLengthInSamples();

    /** The classified hits with their final timing: the analysis result from
        inputProcessor.classifiedHits, copied, with quantisation applied to
        onsetSample. Both the renderer and the MIDI export take their timing
        from here, so the .wav and the .mid always agree. The originals are
        never modified, so the grid can be changed or switched off again.
    */
    std::vector<ClassifiedHit> getTimedHits() const;

    /** Starts playback of either the captured input or the rendered drum loop. */
    void startPreview (PreviewSource source);

    /** The reconstructed drum loop, as built by reconstructLoopFromHits(). */
    const juce::AudioBuffer<float>& getRenderedLoop() const noexcept { return renderedDrumBuffer; }

    std::atomic<bool> recordingEnabled { false };
    std::atomic<bool> recordingStarted { false };
    std::atomic<bool> isPlaybackOn { false };
    juce::AudioBuffer<float> renderedTestBuffer;
    InputProcessor inputProcessor;
    float playbackSpeed = 1;
    std::map<DrumType, int> drumMidiMap;

    // Which drums the rendered loop replaces with samples. An unchecked drum
    // keeps its original audio in place, faded at both ends so the splice into
    // the rendered loop does not click. Read on the message thread at render.
    bool replaceKick = true;
    bool replaceSnare = true;
    bool replaceHat = true;

    bool shouldReplaceNote (int midiNote) const;

    // Quantisation, as set from the settings page and read by getTimedHits().
    // Already in real units - the page converts from its knob positions - so
    // the timing code never sees a slider. Message thread only.
    enum class BpmSource { selected, host, file };

    bool      quantizeEnabled  = false;
    BpmSource bpmSource        = BpmSource::selected;
    double    quantizeBpm      = 120.0;         // the "Select BPM" slider
    double    quantizeDivision = 1.0 / 16.0;    // grid step as a fraction of a whole note
    double    swingAmount      = 0.0;           // 0 straight .. 1 full swing

    /** The tempo read from the uploaded loop's filename, or 0 if it had none.
        Set by processUploadedLoop; a live take clears it.
    */
    double fileBpm = 0.0;

    /** The tempo the grid actually uses, resolved from bpmSource. A source
        with nothing to offer - a file with no tempo in its name, or the host
        outside a DAW - falls back to the selected BPM.
    */
    double effectiveQuantizeBpm() const;

    /** Reads a tempo out of a filename: a run of two or three digits, bounded
        by non-digits, in the range the grid accepts. If several qualify, the
        one written before "bpm" (any case, with or without a separator) wins;
        otherwise the first. Returns 0 when there is none.
    */
    static double bpmFromFileName (const juce::String& fileName);

    // Quantised hits closer together than this are merged into the earlier one.
    static constexpr double minHitSpacingSeconds = 0.05;

    static constexpr double unreplacedHitFadeInSeconds = 0.000;
    static constexpr double unreplacedHitFadeOutSeconds = 0.005;
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

    /** The sounds in drumSynth, keyed by their note, so the renderer can pick a
        drum in constant time regardless of the order they sit in the synth.
        Kept in step with drumSynth by loadSampleFromReader and prepareToPlay.
    */
    std::map<int, DrumSamplerSound::Ptr> drumSoundsByNote;

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
        int hitIndex = -1; // into inputProcessor.storedHits, for keeping the original audio
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
    juce::AudioBuffer<float> inputPreviewBuffer;   // snapshot handed to playAudio for an input preview
    std::atomic<bool> previewingInput { false };

    // The raw input exactly as it arrived - a live take or an uploaded loop - so
    // the input preview replays what was actually heard rather than the extracted
    // hits. Sized once in prepareToPlay: the audio thread appends into it and
    // must never allocate, so anything past the cap is dropped.
    static constexpr double maxCapturedInputSeconds = 60.0;
    juce::AudioBuffer<float> capturedInput;
    std::atomic<int> capturedInputLength { 0 };
    int startSample = 0; // The sample number of the first hit in the input
    bool wasRecording = false;   // audio thread only; detects the start of a take
    int renderedReadPos = 0;
    bool isPlayingRendered = false;
    
    // Filter
    juce::dsp::IIR::Filter<float> bellFilter;
    
    std::ofstream logFile;
    juce::File file = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("ODFAnalysis/ODFValues_new.txt");
    
    bool playprint = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HackBrownAudioProcessor)
};
