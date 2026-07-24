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
    
    
    // Add label (optional)
    //addAndMakeVisible(myLabel);
    myLabel.setText("Volume", juce::dontSendNotification);
    myLabel.attachToComponent(&mySlider, false);  // Attach above slider
    
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
            p.reconstructLoopFromHits();

            //recordButton.setButtonText("Recorded Thing");
            addAndMakeVisible(playButton);
        }
        
        p.recordingEnabled.store(isOn);
    };
    
    playButton.onClick = [&]() {
        p.inputProcessor.reset();
        
        std::cout << p.inputProcessor.storedHitsIndex << std::endl;
        //p.inputProcessor.storedHits;
        //p.renderedTestBuffer = p.inputProcessor.hitsToBuffer();
        p.isPlaybackOn.store(true);
      /*  saveOutput(p.renderedTestBuffer);*/
    };
    
    loopReplaceButton.onClick = [&]() {
        fileOpener([this] (const juce::File& file)
        {
            std::cout << "starting the\n";
            audioProcessor.processUploadedLoop(file);
            if (false) {
                juce::File snaresDir ("/Users/lightspark/Documents/JuceProjects/HackBrown2026/Data/Hats");

                // Defensive check: Ensure the folder actually exists on your disk
                if (snaresDir.isDirectory())
                {
                    for (const auto& entry : juce::RangedDirectoryIterator (snaresDir, true, "*.wav", juce::File::findFiles))
                    {
                        juce::File newFile = entry.getFile();
                        
                        // Process each file one by one
                        audioProcessor.processUploadedLoop (newFile);
                    }
                    
                    DBG ("Finished processing all files in the directory.");
                }
                else
                {
                    DBG ("Error: The directory '/Data/Snares' was not found!");
                }
            }
        });
        
        addAndMakeVisible(playButton);
    };

    kickButton.onClick = [&]() {
        fileOpener([this] (const juce::File& file)
        {
            int midiNote = audioProcessor.drumMidiMap[DrumType::kick];
            audioProcessor.loadSampleFromFile(file, midiNote);
        });
        
        audioProcessor.getLongestSampleLengthInSamples();
    };
    
    snareButton.onClick = [&]() {
        fileOpener([this] (const juce::File& file)
        {
            int midiNote = audioProcessor.drumMidiMap[DrumType::snare];
            audioProcessor.loadSampleFromFile(file, midiNote);
        });
        
        audioProcessor.getLongestSampleLengthInSamples();
    };
    
    hatButton.onClick = [&]() {
        fileOpener([this] (const juce::File& file)
        {
            int midiNote = audioProcessor.drumMidiMap[DrumType::hat];
            audioProcessor.loadSampleFromFile(file, midiNote);
        });
        
        audioProcessor.getLongestSampleLengthInSamples();
    };
    
    // File reader init
    formatManager.registerBasicFormats();
    //transportSource.addChangeListener (this);
    
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    addAndMakeVisible(recordButton);
    addAndMakeVisible(loopReplaceButton);
    addAndMakeVisible(kickButton);
    addAndMakeVisible(snareButton);
    addAndMakeVisible(hatButton);
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
        //g.drawFittedText ("DRUMIFY", getLocalBounds(), juce::Justification::centred, 1);
    }
    
    if (isDragging)
    {
        // Draw a highlighting overlay when dragging a file over the UI
        g.setColour (juce::Colours::orange.withAlpha (0.2f));
        g.fillAll();

        g.setColour (juce::Colours::orange);
        g.drawRect (getLocalBounds(), 3); // 3-pixel border
        
        g.setFont (18.0f);
        g.drawText ("Drop your audio loop here", getLocalBounds(), juce::Justification::centred);
    }
    /*
    if (isHovering)
    {
        g.setColour (juce::Colours::lightgreen);
        g.drawRect (getLocalBounds(), 3); // Draw a thick border
        g.drawText ("Drop it here!", getLocalBounds(), juce::Justification::centred);
    }
    else
    {
        g.drawText ("Drag an audio file here", getLocalBounds(), juce::Justification::centred);
    }*/
}

void HackBrownAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    int windowWidth = 1000;
    int windowHeight = 700;
    
    setSize(1000, 700);
    
    int buttonWidth = 170;
    
    recordButton.setBounds(20, 20, buttonWidth, 40);
    playButton.setBounds(20, 80, buttonWidth, 40);
    mySlider.setBounds(200, 50, 100, 200);
    loopReplaceButton.setBounds((windowWidth - buttonWidth) / 2, 200, buttonWidth, 70);
    
    int importSoundXOffset = 100;
    int importSoundHeight = 70;
    
    kickButton.setBounds(importSoundXOffset, 320, buttonWidth, importSoundHeight);
    snareButton.setBounds(importSoundXOffset, 420, buttonWidth, importSoundHeight);
    hatButton.setBounds(importSoundXOffset, 520, buttonWidth, importSoundHeight);
}

// 1. Tell the OS if you are interested in the files being dragged
bool HackBrownAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;

    // Optional: Only accept audio files
    juce::File file (files[0]);
    juce::String ext = file.getFileExtension().toLowerCase();
    return (ext == ".wav" || ext == ".mp3" || ext == ".aiff" || ext == ".aif" || ext == ".flac");
}

// 2. This triggers when the user releases the mouse and drops the file
void HackBrownAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    isDragging = false;
    repaint();

    if (files.isEmpty())
        return;

    // Grab the first file in the array of dropped items
    juce::File file (files[0]);
    
    // Run your existing processing and show the play button
    audioProcessor.processUploadedLoop(file);
    addAndMakeVisible(playButton);
}

// 3. (Optional) Provide visual feedback when the file enters the UI area
void HackBrownAudioProcessorEditor::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    isDragging = true;
    repaint(); // Forces paint() to run, allowing you to draw a "Drop Here" overlay
}

// 4. (Optional) Reset visual feedback if the user drags the file away
void HackBrownAudioProcessorEditor::fileDragExit (const juce::StringArray& files)
{
    isDragging = false;
    repaint();
}

/*
bool HackBrownAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto file : files)
    {
        if (file.endsWith(".wav") || file.endsWith(".mp3") || file.endsWith(".aif"))
            return true;
    }
    return false;
}

void HackBrownAudioProcessorEditor::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    if ((x > 100 && x < 200) & (y > 500 && y < 600)) {
        isHovering = true;
        repaint();
    }
}

void HackBrownAudioProcessorEditor::fileDragExit (const juce::StringArray& files)
{
    isHovering = false;
    repaint();
}

void HackBrownAudioProcessorEditor::fileDragMove (const juce::StringArray& files, int x, int y)
{
    // You can use x and y to see *where* they are hovering if you have a specific drop-zone
}

void HackBrownAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    isHovering = false;
    repaint();

    // Grab the first file from the array
    juce::File file (files[0]);
    DBG("file dropped");
    
    // Pass it to your processor (make sure to implement this method in your Processor!)
    //audioProcessor.loadDroppedFile (file);
}
*/

void HackBrownAudioProcessorEditor::fileOpener (std::function<void (const juce::File&)> fileAction)
{
    chooser = std::make_unique<juce::FileChooser> ("Select a Wav or mp3 file to use...", juce::File {}, "*.wav");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    // Capture the callback function by value
    chooser->launchAsync (chooserFlags, [this, fileAction] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file != juce::File {})
        {
            DBG ("file chosen");
            // Run the custom code that was passed into fileOpener
            fileAction(file);
        }
    });
}
