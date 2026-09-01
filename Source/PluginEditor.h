/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/** Palette and type shared by the popups. */
namespace DrumifyTheme
{
    inline const juce::Colour panelLeft  { 0xfff8b09c };
    inline const juce::Colour panelRight { 0xfffde5d4 };
    inline const juce::Colour panelEdge  { 0xffcf8f80 };

    inline const juce::Colour popupLeft  { 0xfffdf2ea };
    inline const juce::Colour popupRight { 0xfffbdfd1 };

    inline const juce::Colour ink          { 0xff4d4d4d };
    inline const juce::Colour recordAccent { 0xffd8524a };

    juce::Font mono (float height, bool bold = false);

    void paintPanel (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                     bool highlighted, bool down);
}

//==============================================================================
/** The UI is authored against the 1200x700 canvas of Background.png. */
namespace DrumifyLayout
{
    constexpr int canvasWidth  = 1200;
    constexpr int canvasHeight = 700;

    // Every text label is one of the 692x117 artwork frames, drawn at a single
    // scale so the type size stays consistent across the whole UI.
    constexpr float labelScale = 0.52f;

    // Top row
    constexpr int titleX = 40, titleY = 25, titleW = 410, titleH = 95;
    constexpr int menuRight = 1150, menuCentreY = 60, menuIconHeight = 58, menuGap = 34;

    // The drum layers are exported already aligned to the canvas, so they are
    // drawn full-frame and their caption is placed under the kit.
    constexpr int kitLabelCentreX = 248, kitLabelCentreY = 632;

    /** How far outside a drum's artwork a hover still counts, in canvas pixels.
        Without this the cymbal stands would be almost impossible to point at.
    */
    constexpr float drumHoverTolerance = 8.0f;

    // Microphone column (label above, then the 400x400 mic artwork frame)
    constexpr int micCentreX = 600, micFrameSize = 420;
    constexpr int micTop = 215;   // top of the component, i.e. above the label

    // The about page, which slides in from the left over the centre of the plugin
    constexpr int infoPageX = 250, infoPageY = 84, infoPageW = 700, infoPageH = 556;

    // The speaker and save layers are canvas-aligned too; only their captions
    // need placing.
    constexpr int speakerLabelCentreX = 928, speakerLabelCentreY = 402;
    constexpr int saveLabelCentreX    = 928, saveLabelCentreY    = 658;
}

//==============================================================================
/** A piece of artwork together with the bounds of its non-transparent content,
    so layers exported on a shared canvas can be placed by what they actually
    draw rather than by their padding.
*/
struct AssetLayer
{
    void load (const void* data, int dataSize);

    /** Scales the whole image so that `frame` covers the entire artwork frame.
        Use this for layers that must stay registered with each other.
    */
    void drawFrame (juce::Graphics&, juce::Rectangle<float> frame,
                    float opacity = 1.0f, float highlight = 0.0f) const;

    /** Scales the image so its opaque content exactly fills `target`. */
    void drawContent (juce::Graphics&, juce::Rectangle<float> target,
                      float opacity = 1.0f, float highlight = 0.0f) const;

    /** Draws the whole frame at `scale`, centred on `centre`. */
    void drawScaledAbout (juce::Graphics&, juce::Point<float> centre, float scale,
                          float opacity = 1.0f, float highlight = 0.0f) const;

    /** The largest rect inside `box` with the content's aspect ratio, times `scale`. */
    juce::Rectangle<float> fitContent (juce::Rectangle<float> box, float scale = 1.0f) const;

    /** True when the artwork is opaque at (or within `tolerance` of) `p`, given
        that the whole image is drawn into `frame`. Lets irregular artwork act as
        its own hit region.
    */
    bool hitsContent (juce::Rectangle<float> frame, juce::Point<float> p, float tolerance) const;

    juce::Image image;
    juce::Rectangle<int> content;
};

//==============================================================================
/** Every layer exported for the UI. */
struct DrumifyAssets
{
    DrumifyAssets();

    AssetLayer background, title;
    AssetLayer menuUpdate, menuPlus, menuQuestion;

    AssetLayer kit, kick, snare, hats;
    AssetLayer labelUploadDrumhits, labelUploadKick, labelUploadSnare, labelUploadHat;

    AssetLayer capsuleBack, topNoRing, topRing, bottomNoRing, bottomRing, handle;
    AssetLayer capsuleBackRecording, bottomRecording;
    AssetLayer labelInput, labelRecord, labelStopRecording, labelRecording, labelUploadLoop;

    AssetLayer speakerBg, speakerLeft, speakerRight;
    AssetLayer labelPreviewAudio, labelPreviewInput, labelPreviewOutput;

    AssetLayer savePaint, saveKeyboard, saveLoop;
    AssetLayer labelSaveOutput, labelSaveMidi, labelSaveAudio;
};

//==============================================================================
/** Draws the popups and the controls inside them. */
class DrumifyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DrumifyLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&,
                                   const juce::Path&, juce::Image&) override;
};

//==============================================================================
/** One of the small artwork icons in the top-right corner. */
class AssetButton : public juce::Button
{
public:
    AssetButton (const AssetLayer& layerToDraw, const juce::String& buttonName);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

private:
    const AssetLayer& layer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssetButton)
};

//==============================================================================
/** The drum kit and its caption. The drum layers are canvas-aligned, so hovering
    one simply fades the rest of the kit back and leaves that drum at full
    strength, and the caption changes from "Upload Drumhits" to "Upload <drum>".
*/
class DrumKitComponent : public juce::Component
{
public:
    enum class Zone { none, kick, snare, hats };

    explicit DrumKitComponent (const DrumifyAssets&);

    std::function<void (Zone)> onZoneClicked;

    void paint (juce::Graphics&) override;

    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const AssetLayer* layerFor (Zone) const;
    Zone zoneAt (juce::Point<float>) const;
    void setZone (Zone);

    const DrumifyAssets& assets;
    Zone zone = Zone::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitComponent)
};

//==============================================================================
/** The two preview speakers and their caption. Hovering one fades the section's
    backdrop and the other speaker; clicking previews that side's audio.
*/
class SpeakerComponent : public juce::Component
{
public:
    enum class Zone { none, left, right };

    explicit SpeakerComponent (const DrumifyAssets&);

    std::function<void (Zone)> onZoneClicked;

    void paint (juce::Graphics&) override;

    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    Zone zoneAt (juce::Point<float>) const;
    void setZone (Zone);

    const DrumifyAssets& assets;
    Zone zone = Zone::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeakerComponent)
};

//==============================================================================
/** The two save buttons and their caption. Hovering one fades the section's
    backdrop and the other button; clicking saves that side's file type to disc, dragging enables
    dropping into the DAW.
*/
class SaveComponent : public juce::Component
{
public:
    enum class Zone { none, left, right };

    explicit SaveComponent (const DrumifyAssets&);

    std::function<void (Zone)> onZoneClicked;
    std::function<void (Zone)> onZoneDragged;

    void paint (juce::Graphics&) override;

    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    Zone zoneAt (juce::Point<float>) const;
    void setZone (Zone);

    const DrumifyAssets& assets;
    Zone zone = Zone::none;

    // The zone the press started on, so a drag that wanders still exports the
    // file the user actually grabbed.
    Zone pressedZone = Zone::none;
    bool dragStarted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaveComponent)
};

//==============================================================================
/** The microphone and its caption.

    Hovering the top half of the capsule removes the ringless top shell to reveal
    the back of the capsule (caption "Record"); hovering the bottom half does the
    mirror image, leaving the top shell and its ring in place (caption "Upload
    Loop").
*/
class MicrophoneComponent : public juce::Component
{
public:
    enum class Zone { none, top, bottom };

    explicit MicrophoneComponent (const DrumifyAssets&);

    std::function<void()> onRecordToggled;
    std::function<void()> onUploadLoop;

    void setRecording (bool shouldBeRecording);
    bool isRecording() const noexcept { return recording; }

    void paint (juce::Graphics&) override;

    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getFrame() const;
    juce::Rectangle<float> getCapsule() const;
    float getSplitY() const;
    Zone zoneAt (juce::Point<float>) const;
    void setZone (Zone);

    const DrumifyAssets& assets;
    Zone zone = Zone::none;
    bool recording = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrophoneComponent)
};

//==============================================================================
/** The scrollable "about" text that slides in when the "?" is clicked. */
class InfoPageComponent : public juce::Component
{
public:
    InfoPageComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextEditor body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPageComponent)
};

//==============================================================================
/**
*/
class HackBrownAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       public juce::FileDragAndDropTarget,
                                       private juce::Timer
{
public:
    HackBrownAudioProcessorEditor (HackBrownAudioProcessor&);
    ~HackBrownAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void fileOpener(std::function<void (const juce::File&)> fileAction);
    void loadDrumLoopFromDisk();

    /** Dev tool: runs every .wav under a sample folder through the analyser so
        each detected hit appends a row of features to InputProcessor's CSV.
    */
    void batchAnalyseFolder();
    void saveMidiToDisk();
    void saveAudioToDisk();

    /** Render the current loop to a file. Shared by the save buttons and by the
        drag-out, so a dragged file is always identical to a saved one.
        Both return false (and report why) if there is nothing to write.
    */
    bool writeMidiTo (const juce::File& destination, bool reportFailures);
    bool writeAudioTo (const juce::File& destination, bool reportFailures);

    /** Asks where to save and under what name, then hands the file to `saveAction`.
        `extension` is appended if the chooser hands back a name without one.
    */
    void fileSaver (const juce::String& title,
                    const juce::String& defaultFileName,
                    const juce::String& extension,
                    std::function<void (const juce::File&)> saveAction);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

private:
    /** Writes the export to a scratch file and hands it to the OS as a drag, so
        it can be dropped straight onto a track in the host.
    */
    void startDragExport (SaveComponent::Zone zone);

    /** Scratch folder holding files handed to the host by drag-and-drop. */
    juce::File getDragExportFolder() const;

    /** Slides the main controls out to the right and the about text in from the
        left, or back again. Driven by the timer below.
    */
    void toggleInfoPage();
    void timerCallback() override;
    void showSettingsPopup();
    void toggleRecording();
    void loadDrumSample (DrumType);

    HackBrownAudioProcessor& audioProcessor;
    DrumifyAssets assets;                 // must outlive the components below
    DrumifyLookAndFeel drumifyLookAndFeel;

    AssetButton updateButton   { assets.menuUpdate,   "Rebuild Loop" };
    AssetButton plusButton     { assets.menuPlus,     "Settings" };
    AssetButton questionButton { assets.menuQuestion, "About" };

    InfoPageComponent infoPage;

    // 0 = main controls centred, 1 = about page centred.
    float slideProgress = 0.0f;
    bool infoPageVisible = false;

    DrumKitComponent drumKit { assets };
    SpeakerComponent speakers { assets };
    MicrophoneComponent microphone { assets };
    SaveComponent saveButtons { assets };

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;

    bool isDragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HackBrownAudioProcessorEditor)
};
