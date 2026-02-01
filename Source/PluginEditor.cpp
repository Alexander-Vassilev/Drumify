/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//void saveOutput(juce::AudioBuffer<float> buff)
//{
//    juce::File outputFile("/Users/lightspark/Documents/JuceProjects/HackBrown2026/analysis1.wav");
//    
//    if (outputFile.existsAsFile()) {
//        outputFile.deleteFile();
//    }
//    
//    auto outStream = outputFile.createOutputStream();
//    
//    if (outStream != nullptr) {
//        juce::WavAudioFormat format;
//        std::unique_ptr<juce::AudioFormatWriter> writer(
//            format.createWriterFor(outStream.release(), 44100, buff.getNumChannels(), 32, {}, 0));
//        
//        if (writer != nullptr) {
//            writer->writeFromAudioSampleBuffer(buff, 0, buff.getNumSamples());
//        }
//    }
//}

//==============================================================================
HackBrownAudioProcessorEditor::HackBrownAudioProcessorEditor (HackBrownAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    background = juce::ImageCache::getFromMemory (BinaryData::morning_png,
                                             BinaryData::morning_pngSize);
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
            p.inputProcessor.hitsToBuffer();

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
      /*  saveOutput(p.renderedTestBuffer);*/
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
    
    if (background.isValid())
    {
        g.drawImageWithin (background,
                           0, 0, getWidth(), getHeight(),
                           juce::RectanglePlacement::fillDestination); 
        
        g.setColour (juce::Colours::black.withAlpha (0.35f));

        g.setColour (juce::Colours::white.withAlpha (0.12f));

        // subtle top highlight line
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.setColour (juce::Colours::white.withAlpha(0.9f));
        juce::Font font ("Calibri", 50.0f, juce::Font::bold);
        g.setFont (font);
        g.drawFittedText ("DRUMIFY", getLocalBounds(), juce::Justification::centred, 1);
 
    } else 
    {
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white.withAlpha(0.9f));
    juce::Font font ("Orbitron", 32.0f, juce::Font::plain);
    g.setFont (font);
    g.drawFittedText ("RetroRhythm", getLocalBounds(), juce::Justification::centred, 1);
    }
}

void HackBrownAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    setSize(1000, 700);
    recordButton.setBounds(20, 20, 100, 30);
    playButton.setBounds(20, 60, 100, 30);
}

