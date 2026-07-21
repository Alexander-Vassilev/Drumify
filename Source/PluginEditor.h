/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/*class DropZoneButton : public juce::Component,
                       public juce::FileDragAndDropTarget
{
public:
    DropZoneButton (HackBrownAudioProcessor& p) : processor (p)
    {
        // 1. Initialize the internal standard button
        theButton.setButtonText ("Click to Browse or Drag File Here");
        addAndMakeVisible (theButton);
        
        // Optional: If they click it, you can still trigger code
        theButton.onClick = [this]() {
            // Handle regular clicking here if needed
            DBG("button clicked");
        };
    }

    void resized() override
    {
        // Make the button fill the entire bounds of this component
        theButton.setBounds (getLocalBounds());
    }

    //--- FileDragAndDropTarget Methods ---
    
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto file : files)
        {
            if (file.endsWith (".wav") || file.endsWith (".mp3") || file.endsWith (".aif"))
                return true;
        }
        return false;
    }

    void fileDragEnter (const juce::StringArray& files, int x, int y) override
    {
        theButton.setButtonText ("[ Drop File Now ]");
    }

    void fileDragExit (const juce::StringArray& files) override
    {
        theButton.setButtonText ("Click to Browse or Drag File Here");
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        theButton.setButtonText ("File Loaded!");
        
        juce::File file (files[0]);
        DBG("file droppped!");
        //processor.loadDroppedFile (file);
    }

    //--- Mouse Hover Methods for regular hovering ---
    void mouseEnter (const juce::MouseEvent& event) override
    {
        theButton.setButtonText ("Ready for Drop...");
    }

    void mouseExit (const juce::MouseEvent& event) override
    {
        theButton.setButtonText ("Click to Browse or Drag File Here");
    }

private:
    HackBrownAudioProcessor& processor;
    juce::TextButton theButton; // The actual button component nested inside
};*/



//==============================================================================
/**
*/
class HackBrownAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    HackBrownAudioProcessorEditor (HackBrownAudioProcessor&);
    ~HackBrownAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    void fileOpener(std::function<void (const juce::File&)> fileAction);
    /*
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
        
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
*/
    // 2. Add these FileDragAndDropTarget overrides
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    HackBrownAudioProcessor& audioProcessor;
    juce::TextButton recordButton { "Record" };
    juce::TextButton playButton { "Playback" };
    juce::Image background;
    juce::Slider mySlider;
    juce::Label myLabel;
    
    bool isHovering = false;
    juce::TextButton kickButton { "Drop Kick Here" };
    juce::TextButton snareButton { "Drop Snare Here" };
    juce::TextButton hatButton { "Drop Hat Here" };
    
    juce::TextButton loopReplaceButton { "Drop Loop Here" };
    
    //DropZoneButton dzb = DropZoneButton(audioProcessor);
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    
    bool isDragging = false; // Useful for drawing visual feedback

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HackBrownAudioProcessorEditor)
};
