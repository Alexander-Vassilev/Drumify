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

    // Top row. The title is a canvas-aligned layer; only the icons need placing.
    constexpr int menuRight = 1150, menuCentreY = 60, menuIconHeight = 58, menuGap = 34;
    constexpr int homeIconHeight = 58;

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

    // The about page is canvas-aligned artwork - its heading sits in the title
    // slot - with the copy in a column down the parchment, inside its torn
    // edges. The column runs to the very bottom of the canvas: the text is
    // dragged by hand, and should leave by the edge rather than stop short of
    // it.
    constexpr int aboutBodyX = 365, aboutBodyY = 190, aboutBodyW = 500;
    constexpr float aboutBodyFontHeight = 20.0f, aboutBodyLineGap = 5.0f;

    // A line that opens with a short label and a colon is a heading, and is set
    // in bold. The cap keeps a colon further into a sentence from qualifying -
    // the longest real heading, "Lock BPM to host:", is seventeen characters.
    constexpr int aboutHeadingMaxLength = 20;

    // How deep the band is in which the copy dissolves as it leaves the top.
    constexpr int aboutFadeHeight = 70;

    // Empty sheet past the last line, so the end of the copy can be pulled up
    // clear of the bottom edge instead of resting against it.
    constexpr int aboutBodyTailSpace = 40;

    // The copy sits a little off the parchment rather than flat on it.
    constexpr int   aboutShadowRadius = 3;
    constexpr float aboutShadowAlpha  = 0.35f;
    constexpr int   aboutShadowRoom   = 10;   // spare rows so the last line's shadow is not clipped

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
    AssetLayer menuUpdate, menuSettings, menuQuestion;

    AssetLayer kit, kick, snare, hats;
    AssetLayer labelUploadDrumhits, labelUploadKick, labelUploadSnare, labelUploadHat;

    AssetLayer capsuleBack, topNoRing, topRing, bottomNoRing, bottomRing, handle;
    AssetLayer capsuleBackRecording, bottomRecording;
    AssetLayer labelInput, labelRecord, labelStopRecording, labelRecording, labelUploadLoop;

    AssetLayer speakerBg, speakerLeft, speakerRight;
    AssetLayer labelPreviewAudio, labelPreviewInput, labelPreviewOutput, labelStopPlayback;

    AssetLayer savePaint, saveKeyboard, saveLoop;
    AssetLayer labelSaveOutput, labelSaveMidi, labelSaveAudio;

    // The settings page: its static artwork, the checkmark stamped onto a
    // ticked box, and the slider as a vertical strip of every knob position.
    AssetLayer settingsMenu, checkmark, sliderStrip;

    // The about page: its heading in the title slot, and the sheet the copy
    // is written on.
    AssetLayer about, parchment;

    // The top-row icon that returns to the main page from either page.
    AssetLayer home;
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

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    static constexpr float tooltipFontHeight = 16.0f;
};

//==============================================================================
/** One of the small artwork icons in the top-right corner. */
class AssetButton : public juce::Button
{
public:
    /** `tooltip` says what the button does, since none of the icons carry a
        caption; it falls back to the name when there is nothing to add.
    */
    AssetButton (const AssetLayer& layerToDraw, const juce::String& buttonName,
                 const juce::String& tooltip = {});

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

    /** Which drum a point (in this component's coordinates) lands on. Public so
        the editor can route a dropped file the same way it routes a click.
    */
    Zone zoneAt (juce::Point<float>) const;

    /** Sets the highlighted drum directly. The mouse does this itself; a file
        being dragged over the window does not reach this component, so the
        editor drives it to give the drag the same look as a hover.
    */
    void setZone (Zone);

private:
    const AssetLayer* layerFor (Zone) const;

    const DrumifyAssets& assets;
    Zone zone = Zone::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitComponent)
};

//==============================================================================
/** The two preview speakers and their caption. Hovering one fades the section's
    backdrop and the other speaker; clicking previews that side's audio.
*/
class SpeakerComponent : public juce::Component,
                         private juce::Timer
{
public:
    enum class Zone { none, left, right };

    explicit SpeakerComponent (const DrumifyAssets&);
    ~SpeakerComponent() override;

    std::function<void (Zone)> onZoneClicked;

    /** Polled while a speaker is marked as playing, so the "Stop Playback"
        caption clears itself once the audio runs out rather than waiting for a
        click. Playback ends on the audio thread, which cannot tell us directly.
    */
    std::function<bool()> isPlaybackActive;

    /** Marks which speaker is currently playing (or none). Hovering that speaker
        shows "Stop Playback" instead of its preview caption.
    */
    void setPlayingZone (Zone);
    Zone getPlayingZone() const noexcept { return playingZone; }

    void paint (juce::Graphics&) override;

    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    Zone zoneAt (juce::Point<float>) const;
    void setZone (Zone);

    const DrumifyAssets& assets;
    Zone zone = Zone::none;
    Zone playingZone = Zone::none;

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

    /** Sets the hovered half directly - see DrumKitComponent::setZone. */
    void setZone (Zone);

private:
    juce::Rectangle<float> getFrame() const;
    juce::Rectangle<float> getCapsule() const;
    float getSplitY() const;
    Zone zoneAt (juce::Point<float>) const;

    const DrumifyAssets& assets;
    Zone zone = Zone::none;
    bool recording = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrophoneComponent)
};

//==============================================================================
/** The about page that slides in when the "?" is clicked: the heading artwork,
    a sheet of parchment, and the copy written down it. There is no scrollbar -
    the sheet is dragged by hand - and the copy dissolves into the page as it
    leaves the top rather than being cut off at a hard edge.
*/
class InfoPageComponent : public juce::Component
{
public:
    explicit InfoPageComponent (const DrumifyAssets&);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** How far the sheet has been dragged, so a new editor can pick it up. */
    float getScroll() const noexcept   { return scrollOffset; }
    void setScroll (float newOffset);

private:
    /** The column the copy is laid out in, down to the bottom of the canvas. */
    juce::Rectangle<int> bodyArea() const;

    /** Renders the whole of the copy, with its shadow, into pageImage. */
    void buildPageImage();

    /** The top band of the column, with the copy fading out as it leaves. */
    void drawFadingTop (juce::Graphics&, juce::Rectangle<int> area);

    const DrumifyAssets& assets;

    juce::TextLayout layout;        // the copy, laid out to the column width
    juce::Image pageImage;          // that copy drawn once, shadow and all
    juce::Image fadeBand;           // scratch for the dissolving band
    float scrollOffset = 0.0f;
    float dragStartOffset = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfoPageComponent)
};

//==============================================================================
/** The settings page. The labels and empty boxes are one static image; the
    checkmarks, sliders and value readouts are drawn over it from state held
    here. Only the replace toggles and the stretch factor drive anything yet -
    the rest is interaction without effect, until the features behind it exist.
*/
class SettingsPageComponent : public juce::Component,
                              public juce::TooltipClient
{
public:
    SettingsPageComponent (const DrumifyAssets&, HackBrownAudioProcessor&);

    void paint (juce::Graphics&) override;

    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** The page is one component, so both of these answer for whichever
        control the mouse is over rather than for the page as a whole.
    */
    juce::String getTooltip() override;
    juce::MouseCursor getMouseCursor() override;

private:
    enum Check { kicks, snares, hats, quantize, lockHost, lockFile, selectBpm, swing,
                 stretchOutput, musicalStretch, numChecks };
    enum Slide { sensitivity, quantizeAmount, bpm, swingAmount, stretch,
                 biasKick, biasSnare, biasHat, numSlides };

    /** How much of the pipeline a change has to run again, from the cheapest
        (place the events again) to the dearest (detect the onsets again).
    */
    enum class Rerender { none, timing, classes, detection };

    /** Whether a control can currently be used, per the page's enabling rules. */
    bool isEnabled (Check) const;
    bool isEnabled (Slide) const;

    int checkAt (juce::Point<int>) const;   // index, or -1
    int slideAt (juce::Point<int>) const;

    void toggle (Check);
    void setSlider (Slide, float value);
    float valueForX (Slide, int x) const;

    juce::String readout (Slide) const;

    /** The slider's rectangle in canvas coordinates; the bias sliders are
        drawn smaller than the artwork's own.
    */
    juce::Rectangle<int> sliderBounds (Slide) const;
    float sliderScale (Slide) const;

    /** Pushes the page's state into the processor, then re-runs as much of
        the pipeline as `level` asks for.
    */
    void applyToProcessor (Rerender level);

    /** Keeps the processor's copy of the knob positions up to date, and seeds
        them from it when a reopened editor builds this page again.
    */
    void saveState();
    bool restoreState();

    const DrumifyAssets& assets;
    HackBrownAudioProcessor& processor;

    std::array<bool, numChecks> checked {};
    std::array<float, numSlides> value {};   // 0..1, the knob's position
    int dragging = -1;                        // Slide being dragged, or -1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPageComponent)
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
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

private:
    /** Writes the export to a scratch file and hands it to the OS as a drag, so
        it can be dropped straight onto a track in the host.
    */
    void startDragExport (SaveComponent::Zone zone);

    /** Scratch folder holding files handed to the host by drag-and-drop. */
    juce::File getDragExportFolder() const;

    /** Slides the main controls out to one side and a page in from the other,
        or back again. The about page comes in from the left, settings from the
        right; opening either closes the other. Driven by the timer below.
    */
    void toggleInfoPage();
    void toggleSettingsPage();
    void goHome();

    /** Records which page is open, so a reopened editor returns to it. */
    void rememberOpenPage();
    void timerCallback() override;
    void toggleRecording();
    void loadDrumSample (DrumType);

    /** Loads `file` as the sample for `drum`. The single path behind both the
        click-then-choose flow and a file dropped straight onto the drum.
    */
    void applyDrumSample (DrumType, const juce::File&);

    /** What a file dropped at an editor-space point would do. The three drums
        route to their sample slot; anywhere else - which includes the lower
        half of the capsule, whose click action is the same thing - uploads the
        file as the loop, as a drop anywhere on the window always has.
    */
    enum class DropTarget { loop, kick, snare, hats };
    DropTarget dropTargetAt (juce::Point<int> editorPoint) const;

    /** Lights up whatever a drop at this point would hit, exactly as a mouse
        hover would: the drum under the pointer, or - off the drums - the
        capsule's upload state, since that is where the file will go.
    */
    void showDragTarget (juce::Point<int> editorPoint);
    void clearDragTarget();

    HackBrownAudioProcessor& audioProcessor;
    DrumifyAssets assets;                 // must outlive the components below
    DrumifyLookAndFeel drumifyLookAndFeel;

    AssetButton updateButton   { assets.menuUpdate,   "Rebuild Loop",
                                 "Analyse the last take again and rebuild the loop" };
    AssetButton settingsButton { assets.menuSettings, "Settings",
                                 "Settings menu..." };
    AssetButton questionButton { assets.menuQuestion, "About",
                                 "About Drumify and how to use it!" };

    // Takes the "?" slot while either page is open, and is the only way back.
    AssetButton homeButton     { assets.home,         "Home",
                                 "Back to the main page" };

    InfoPageComponent infoPage { assets };
    SettingsPageComponent settingsPage { assets, audioProcessor };

    // -1 = settings page centred, 0 = main controls centred, +1 = about page.
    float slideProgress = 0.0f;
    bool infoPageVisible = false;
    bool settingsPageVisible = false;

    DrumKitComponent drumKit { assets };
    SpeakerComponent speakers { assets };
    MicrophoneComponent microphone { assets };
    SaveComponent saveButtons { assets };

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;



    // Parented to the editor rather than the desktop, which is what a plugin
    // wants: a desktop tooltip window misbehaves inside some hosts.
    juce::TooltipWindow tooltipWindow { this, 1000 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HackBrownAudioProcessorEditor)
};
