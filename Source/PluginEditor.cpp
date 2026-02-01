/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HackBrownAudioProcessorEditor::HackBrownAudioProcessorEditor (HackBrownAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    recordButton.setClickingTogglesState(true);
    recordButton.setToggleState(false, juce::dontSendNotification);
    
    recordButton.onClick = [&]() {
        bool isOn = recordButton.getToggleState();
        auto message = "Recording!";
        if (!recordButton.getToggleState()) {
            message = "Record";
        }
        //const auto message =  ? "Recording!" : "Record";
        recordButton.setButtonText(message);
        
        if (!isOn && p.recordingStarted.load()) {
<<<<<<< HEAD
            // algorithm classifies/do classification

                    p.inputProcessor.classifyStoredHits(p.getSampleRate());

                    DBG("---- Editor sees classified hits ----");
                    for (const auto& ch : p.inputProcessor.classifiedHits) //hits are stored in inputProcessor.classifiedhits
                    {
                        DBG("Hit index " << ch.hitIndex
                            << " classified as "
                            << HitClassifier::toString(ch.type));
                    }
                



            recordButton.setButtonText("Recorded Thing");
            
=======
            // Call Gabe FFT Algorithm Here
            // p.startRecording();
            // Call Gabe FFT Algorithm Here  --> do classification instead
            p.inputProcessor.classifyStoredHits(p.getSampleRate());
            recordButton.setButtonText("Recorded Thing");
            addAndMakeVisible(playButton);
            
            for (int i = 0; i < p.inputProcessor.storedHitsIndex; ++i) {
                const auto& hit = p.inputProcessor.storedHits[i];

                DBG("Editor sees Hit #" << i
                    << " classified as "
                    << HitClassifier::toString(hit.type));
            }
>>>>>>> f3e6679 (button fixed)

/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HackBrownAudioProcessorEditor::HackBrownAudioProcessorEditor (HackBrownAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    recordButton.setClickingTogglesState(true);
    recordButton.setToggleState(false, juce::dontSendNotification);
    
    recordButton.onClick = [&]() {
        bool isOn = recordButton.getToggleState();
        auto message = "Recording!";
        if (!recordButton.getToggleState()) {
            message = "Record";
        }
        //const auto message =  ? "Recording!" : "Record";
        recordButton.setButtonText(message);
        
        if (!isOn && p.recordingStarted.load()) {
            // algorithm classifies/do classification
            p.inputProcessor.classifyStoredHits(p.getSampleRate());

            DBG("---- Editor sees classified hits ----");
            for (const auto& ch : p.inputProcessor.classifiedHits) //hits are stored in inputProcessor.classifiedhits
            {
                DBG("Hit index " << ch.hitIndex
                    << " classified as "
                    << HitClassifier::toString(ch.type));
            }

            recordButton.setButtonText("Recorded Thing");
            // Call Gabe FFT Algorithm Here
            // p.startRecording();
            // Call Gabe FFT Algorithm Here  --> do classification instead
            addAndMakeVisible(playButton);
        }
        
        p.recordingEnabled.store(isOn);
    };
    
    playButton.onClick = [&]() {
        p.isPlaybackOn.store(true);
        
        std::cout << p.inputProcessor.storedHitsIndex << std::endl;
        //p.inputProcessor.storedHits;
        p.renderedTestBuffer = p.inputProcessor.hitsToBuffer();
    };

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    addAndMakeVisible(recordButton);
    setSize (400, 300);
}

HackBrownAudioProcessorEditor::~HackBrownAudioProcessorEditor()
{
}

//==============================================================================
void HackBrownAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void HackBrownAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    recordButton.setBounds(20, 20, 100, 30);
    playButton.setBounds(20, 60, 100, 30);
}

