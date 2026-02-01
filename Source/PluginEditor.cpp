/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void saveOutput(juce::AudioBuffer<float> buff)
{
    juce::File outputFile("/Users/lightspark/Documents/JuceProjects/HackBrown2026/analysis1.wav");
    
    if (outputFile.existsAsFile()) {
        outputFile.deleteFile();
    }
    
    auto outStream = outputFile.createOutputStream();
    
    if (outStream != nullptr) {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(outStream.release(), 44100, buff.getNumChannels(), 32, {}, 0));
        
        if (writer != nullptr) {
            writer->writeFromAudioSampleBuffer(buff, 0, buff.getNumSamples());
        }
    }
}

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
            p.buildDrumBuffer();

            DBG("---- Editor sees classified hits ----");
            for (const auto& ch : p.inputProcessor.classifiedHits) //hits are stored in inputProcessor.classifiedhits
            {
                DBG("Hit index " << ch.hitIndex
                    << " classified as "
                    << HitClassifier::toString(ch.type));
            }

            recordButton.setButtonText("Recorded Thing");
            addAndMakeVisible(playButton);
        }
        
        p.recordingEnabled.store(isOn);
    };
    
    playButton.onClick = [&]() {
        p.isPlaybackOn.store(true);
        
        std::cout << p.inputProcessor.storedHitsIndex << std::endl;
        //p.inputProcessor.storedHits;
        p.renderedTestBuffer = p.inputProcessor.hitsToBuffer();
        saveOutput(p.renderedTestBuffer);
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

