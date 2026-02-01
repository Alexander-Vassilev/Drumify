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
    
    recordButton.onClick = [&]() {
        bool isOn = recordButton.getToggleState();
        const auto message = recordButton.getToggleState() ? "Recording!" : "Record";
        recordButton.setButtonText(message);
        
        if (!isOn && p.recordingStarted.load()) {
            // Call Gabe FFT Algorithm Here
            // p.startRecording();
            if (!isOn && p.recordingStarted.load())
            {
                // Call Gabe FFT Algorithm Here  --> do classification instead
                p.inputProcessor.classifyStoredHits(p.getSampleRate());

                recordButton.setButtonText("Recorded Thing");
            }
            
            for (int i = 0; i < p.inputProcessor.storedHitsIndex; ++i)
            {
                const auto& hit = p.inputProcessor.storedHits[i];

                DBG("Editor sees Hit #" << i
                    << " classified as "
                    << HitClassifier::toString(hit.type));
            }

            recordButton.setButtonText("Recorded Thing");
            
        }
        
        p.recordingEnabled.store(!isOn);
    };
    
    playButton.onClick = [&]() {
        p.isPlaybackOn.store(true);
        recordButton.setButtonText("Clicked!");
        
        std::cout << p.inputProcessor.storedHitsIndex << std::endl;
        //p.inputProcessor.storedHits;
        p.renderedTestBuffer = p.inputProcessor.hitsToBuffer();
    };

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    addAndMakeVisible(recordButton);
    addAndMakeVisible(playButton);
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

