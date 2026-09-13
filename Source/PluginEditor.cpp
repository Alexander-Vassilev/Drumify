/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "hitClassifier.h"

//==============================================================================
// The copy shown on the about page behind the "?" button in the top row.
static const char* const infoPanelText =
    "Welcome to Drumify! This is an experience that lets you transform any drumloop to your "
    "heart's desire. Beatbox into the microphone or import loops of your own and Drumify's "
    "algorithm will replace all of the kicks, snares, and hats with your own samples. Enjoy!\n"
    "\n"
    "Motivation:\n"
    "Drumify started with my firm belief that our mouth is humanity's most intuitive instrument. "
    "I wanted to create a fun and unique tool that allows users of all levels of musical "
    "experience to enjoy a fresh drum workflow. Rather than having to draw out a loop by hand or "
    "buy a midi controller, you can just beatbox your desired drumloop into the microphone and "
    "import it into your DAW, or do whatever you want with it, really.\n"
    "\n"
    "\"Well, I do not really like using my own voice\" -Hypothetical reader\n"
    "\n"
    "I hear you loud and clear, which is why Drumify has a second input method: audio files. If "
    "you ever find a really cool drum loop on Splice or out in the wild and want to replace the "
    "drum hits with your own sounds, you can!\n"
    "\n"
    "Instructions:\n"
    "\"Woah, this does not look like any VST I have ever used\" -Hypothetical reader\n"
    "\n"
    "I hear you loud and clear, which is why I included this video for a live demo: (link)\n"
    "\n"
    "If you prefer to read, here are the instructions:\n"
    "\n"
    "Drumset: Click individual drums to import your own samples, which can be kicks, snares, or "
    "hats, although feel free to experiment and throw in whatever you want!\n"
    "\n"
    "Microphone: Click the top half of the capsule to record beatboxing or any live sound, then "
    "click again to stop. As an alternative input method, click the bottom half of the capsule "
    "to load an input loop. After doing either of these, Drumify will detect all of the drum hits "
    "and replace them in real time!\n"
    "\n"
    "Speakers: The left speaker previews your input so you can hear the original, and the right "
    "speaker plays the replaced drumhits!\n"
    "\n"
    "Save Buttons: If you are happy with the output, click the left button to save a midi file "
    "and/or click the right one to save an audio file. Alternatively, you can drag and drop "
    "directly into your session.\n"
    "\n"
    "Menu: The question mark shows the info page (You happen to be looking at it), which you can "
    "revisit at any time. The plus button shows advanced settings, which lets you change "
    "Drumify's algorithm. The rotating arrows update the output, if you are happy with the "
    "input but replaced your samples or changed the algorithm.\n";

//==============================================================================
juce::Font DrumifyTheme::mono (float height, bool bold)
{
    static const juce::String faceName = []
    {
        const auto installed = juce::Font::findAllTypefaceNames();

        for (const auto* preferred : { "Courier New", "Courier" })
            if (installed.contains (preferred, true))
                return juce::String (preferred);

        return juce::Font::getDefaultMonospacedFontName();
    }();

    return juce::Font (juce::FontOptions {}
                          .withName (faceName)
                          .withHeight (height)
                          .withStyle (bold ? "Bold" : "Regular"));
}

void DrumifyTheme::paintPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                               float corner, bool highlighted, bool down)
{
    auto left  = panelLeft;
    auto right = panelRight;

    if (down)
    {
        left  = left.darker (0.10f);
        right = right.darker (0.10f);
    }
    else if (highlighted)
    {
        left  = left.brighter (0.06f);
        right = right.brighter (0.06f);
    }

    juce::Path shape;
    shape.addRoundedRectangle (bounds, corner);

    juce::DropShadow (juce::Colours::black.withAlpha (down ? 0.18f : 0.28f),
                      down ? 5 : 9,
                      { 2, down ? 2 : 4 }).drawForPath (g, shape);

    g.setGradientFill (juce::ColourGradient (left,  bounds.getX(),     bounds.getCentreY(),
                                             right, bounds.getRight(), bounds.getCentreY(), false));
    g.fillPath (shape);

    g.setColour (panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.2f);
}

//==============================================================================
/** Scans the alpha channel for the bounds of everything actually drawn. */
static juce::Rectangle<int> findOpaqueBounds (const juce::Image& image)
{
    if (! image.isValid())
        return {};

    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

    if (data.pixelFormat != juce::Image::ARGB)
        return image.getBounds();

    int minX = image.getWidth(), minY = image.getHeight(), maxX = -1, maxY = -1;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            if (reinterpret_cast<const juce::PixelARGB*> (data.getPixelPointer (x, y))->getAlpha() > 8)
            {
                minX = juce::jmin (minX, x);
                maxX = juce::jmax (maxX, x);
                minY = juce::jmin (minY, y);
                maxY = juce::jmax (maxY, y);
            }
        }
    }

    if (maxX < 0)
        return {};

    return { minX, minY, maxX - minX + 1, maxY - minY + 1 };
}

void AssetLayer::load (const void* data, int dataSize)
{
    image = juce::ImageCache::getFromMemory (data, dataSize);
    content = findOpaqueBounds (image);
}

void AssetLayer::drawFrame (juce::Graphics& g, juce::Rectangle<float> frame,
                            float opacity, float highlight) const
{
    if (! image.isValid() || frame.isEmpty())
        return;

    const auto transform = juce::AffineTransform::scale (frame.getWidth()  / (float) image.getWidth(),
                                                         frame.getHeight() / (float) image.getHeight())
                             .translated (frame.getX(), frame.getY());

    g.setOpacity (opacity);
    g.drawImageTransformed (image, transform, false);

    if (highlight > 0.0f)
    {
        // Filling the alpha channel with a flat brush brightens the artwork
        // without washing out its silhouette.
        g.setColour (juce::Colours::white.withAlpha (highlight));
        g.drawImageTransformed (image, transform, true);
    }

    g.setOpacity (1.0f);
}

void AssetLayer::drawContent (juce::Graphics& g, juce::Rectangle<float> target,
                              float opacity, float highlight) const
{
    if (content.isEmpty())
        return;

    const auto sx = target.getWidth()  / (float) content.getWidth();
    const auto sy = target.getHeight() / (float) content.getHeight();

    drawFrame (g, { target.getX() - (float) content.getX() * sx,
                    target.getY() - (float) content.getY() * sy,
                    (float) image.getWidth()  * sx,
                    (float) image.getHeight() * sy },
               opacity, highlight);
}

void AssetLayer::drawScaledAbout (juce::Graphics& g, juce::Point<float> centre, float scale,
                                  float opacity, float highlight) const
{
    if (! image.isValid())
        return;

    drawFrame (g, juce::Rectangle<float> ((float) image.getWidth()  * scale,
                                          (float) image.getHeight() * scale).withCentre (centre),
               opacity, highlight);
}

bool AssetLayer::hitsContent (juce::Rectangle<float> frame, juce::Point<float> p, float tolerance) const
{
    if (! image.isValid() || frame.isEmpty())
        return false;

    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

    if (data.pixelFormat != juce::Image::ARGB)
        return frame.contains (p);

    const auto sx = (float) image.getWidth()  / frame.getWidth();
    const auto sy = (float) image.getHeight() / frame.getHeight();

    const juce::Point<float> offsets[]
    {
        { 0.0f, 0.0f },
        { -tolerance, 0.0f }, { tolerance, 0.0f },
        { 0.0f, -tolerance }, { 0.0f, tolerance },
        { -tolerance, -tolerance }, { tolerance, -tolerance },
        { -tolerance, tolerance }, { tolerance, tolerance }
    };

    for (const auto& offset : offsets)
    {
        const auto x = juce::roundToInt ((p.x + offset.x - frame.getX()) * sx);
        const auto y = juce::roundToInt ((p.y + offset.y - frame.getY()) * sy);

        if (juce::isPositiveAndBelow (x, image.getWidth())
             && juce::isPositiveAndBelow (y, image.getHeight())
             && reinterpret_cast<const juce::PixelARGB*> (data.getPixelPointer (x, y))->getAlpha() > 30)
            return true;
    }

    return false;
}

juce::Rectangle<float> AssetLayer::fitContent (juce::Rectangle<float> box, float scale) const
{
    if (content.isEmpty())
        return box;

    const auto aspect = (float) content.getWidth() / (float) content.getHeight();

    auto w = box.getWidth();
    auto h = w / aspect;

    if (h > box.getHeight())
    {
        h = box.getHeight();
        w = h * aspect;
    }

    return juce::Rectangle<float> (w * scale, h * scale).withCentre (box.getCentre());
}

//==============================================================================
DrumifyAssets::DrumifyAssets()
{
    background.load (BinaryData::Background_png,   BinaryData::Background_pngSize);
    title.load      (BinaryData::DrumifyTitle_png, BinaryData::DrumifyTitle_pngSize);

    menuUpdate.load   (BinaryData::Menuupdate_png,   BinaryData::Menuupdate_pngSize);
    menuPlus.load     (BinaryData::MenuPlus_png,     BinaryData::MenuPlus_pngSize);
    menuQuestion.load (BinaryData::MenuQuestion_png, BinaryData::MenuQuestion_pngSize);

    kit.load   (BinaryData::DrumkitFullKit_png, BinaryData::DrumkitFullKit_pngSize);
    kick.load  (BinaryData::DrumkitKick_png,    BinaryData::DrumkitKick_pngSize);
    snare.load (BinaryData::DrumkitSnare_png,   BinaryData::DrumkitSnare_pngSize);
    hats.load  (BinaryData::DrumkitHats_png,    BinaryData::DrumkitHats_pngSize);

    labelUploadDrumhits.load (BinaryData::DrumkitUploadDrumhits_png, BinaryData::DrumkitUploadDrumhits_pngSize);
    labelUploadKick.load     (BinaryData::DrumkitUploadKick_png,     BinaryData::DrumkitUploadKick_pngSize);
    labelUploadSnare.load    (BinaryData::DrumkitUploadSnare_png,    BinaryData::DrumkitUploadSnare_pngSize);
    labelUploadHat.load      (BinaryData::DrumkitUploadHat_png,      BinaryData::DrumkitUploadHat_pngSize);

    capsuleBack.load  (BinaryData::CapsuleBack_png,             BinaryData::CapsuleBack_pngSize);
    topNoRing.load    (BinaryData::MicTopCapsuleNoRing_png,     BinaryData::MicTopCapsuleNoRing_pngSize);
    topRing.load      (BinaryData::MicTopCapsuleRing_png,       BinaryData::MicTopCapsuleRing_pngSize);
    bottomNoRing.load (BinaryData::MicBottomCapsuleNoRing_png,  BinaryData::MicBottomCapsuleNoRing_pngSize);
    bottomRing.load   (BinaryData::MicBottomCapsuleRing_png,    BinaryData::MicBottomCapsuleRing_pngSize);
    handle.load       (BinaryData::MicHandle_png,               BinaryData::MicHandle_pngSize);

    capsuleBackRecording.load (BinaryData::MicCapsuleBackRecording_png,   BinaryData::MicCapsuleBackRecording_pngSize);
    bottomRecording.load      (BinaryData::MicBottomCapsuleRecording_png, BinaryData::MicBottomCapsuleRecording_pngSize);

    labelInput.load         (BinaryData::MicInput_png,         BinaryData::MicInput_pngSize);
    labelRecord.load        (BinaryData::MicRecord_png,        BinaryData::MicRecord_pngSize);
    labelStopRecording.load (BinaryData::MicStopRecording_png, BinaryData::MicStopRecording_pngSize);
    labelRecording.load     (BinaryData::MicRecording_png,     BinaryData::MicRecording_pngSize);
    labelUploadLoop.load    (BinaryData::MicUploadLoop_png,    BinaryData::MicUploadLoop_pngSize);

    speakerBg.load    (BinaryData::SpeakersBG_png,    BinaryData::SpeakersBG_pngSize);
    speakerLeft.load  (BinaryData::SpeakersLeft_png,  BinaryData::SpeakersLeft_pngSize);
    speakerRight.load (BinaryData::SpeakersRight_png, BinaryData::SpeakersRight_pngSize);

    labelPreviewAudio.load  (BinaryData::SpeakersPreviewAudio_png,  BinaryData::SpeakersPreviewAudio_pngSize);
    labelPreviewInput.load  (BinaryData::SpeakersPreviewInput_png,  BinaryData::SpeakersPreviewInput_pngSize);
    labelPreviewOutput.load (BinaryData::SpeakersPreviewOutput_png, BinaryData::SpeakersPreviewOutput_pngSize);
    labelStopPlayback.load  (BinaryData::SpeakersStopPlayback_png,  BinaryData::SpeakersStopPlayback_pngSize);

    savePaint.load       (BinaryData::SavePaint_png,       BinaryData::SavePaint_pngSize);
    saveKeyboard.load    (BinaryData::SaveKeyboard_png,    BinaryData::SaveKeyboard_pngSize);
    saveLoop.load        (BinaryData::SaveLoop_png,        BinaryData::SaveLoop_pngSize);
    labelSaveMidi.load   (BinaryData::SaveMIDI_png,        BinaryData::SaveMIDI_pngSize);
    labelSaveAudio.load  (BinaryData::SaveAudio_png,       BinaryData::SaveAudio_pngSize);

    settingsMenu.load (BinaryData::SettingsMenuStatic_png, BinaryData::SettingsMenuStatic_pngSize);
    checkmark.load    (BinaryData::Checkmark_png,          BinaryData::Checkmark_pngSize);
    sliderStrip.load  (BinaryData::SliderStrip_png,        BinaryData::SliderStrip_pngSize);
    about.load        (BinaryData::About_png,              BinaryData::About_pngSize);
    labelSaveOutput.load (BinaryData::SaveSaveOutput_png,  BinaryData::SaveSaveOutput_pngSize);
}

//==============================================================================
DrumifyLookAndFeel::DrumifyLookAndFeel()
{
    setColour (juce::TextButton::textColourOffId, DrumifyTheme::ink);
    setColour (juce::TextButton::textColourOnId,  DrumifyTheme::ink);
    setColour (juce::ScrollBar::thumbColourId,    DrumifyTheme::ink);
    setColour (juce::Label::textColourId,         DrumifyTheme::ink);

    setColour (juce::Slider::backgroundColourId,        DrumifyTheme::panelEdge.withAlpha (0.35f));
    setColour (juce::Slider::trackColourId,             DrumifyTheme::ink.withAlpha (0.55f));
    setColour (juce::Slider::thumbColourId,             DrumifyTheme::ink);
    setColour (juce::Slider::textBoxTextColourId,       DrumifyTheme::ink);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::white.withAlpha (0.45f));
    setColour (juce::Slider::textBoxOutlineColourId,    DrumifyTheme::panelEdge);
}

juce::Font DrumifyLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return DrumifyTheme::mono (juce::jlimit (12.0f, 18.0f, (float) buttonHeight * 0.32f));
}

void DrumifyLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour&,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (4.0f);
    DrumifyTheme::paintPanel (g, bounds, juce::jmin (bounds.getHeight(), bounds.getWidth()) * 0.22f,
                              shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

void DrumifyLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (DrumifyTheme::ink);

    juce::StringArray lines;
    lines.addLines (button.getButtonText());

    const auto lineHeight = font.getHeight() * 1.28f;
    auto y = (float) button.getHeight() * 0.5f - lineHeight * (float) lines.size() * 0.5f;

    for (const auto& line : lines)
    {
        g.drawText (line, juce::Rectangle<float> (0.0f, y, (float) button.getWidth(), lineHeight),
                    juce::Justification::centred);
        y += lineHeight;
    }
}

void DrumifyLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g,
                                                   const juce::Path& path, juce::Image&)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.30f), 12, { 0, 5 }).drawForPath (g, path);

    const auto area = path.getBounds();
    g.setGradientFill (juce::ColourGradient (DrumifyTheme::popupLeft,  area.getX(),     area.getY(),
                                             DrumifyTheme::popupRight, area.getRight(), area.getBottom(), false));
    g.fillPath (path);

    g.setColour (DrumifyTheme::panelEdge);
    g.strokePath (path, juce::PathStrokeType (1.4f));
}

//==============================================================================
AssetButton::AssetButton (const AssetLayer& layerToDraw, const juce::String& buttonName)
    : juce::Button (buttonName), layer (layerToDraw)
{
    setTooltip (buttonName);
}

void AssetButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown)
{
    const auto scale = shouldDrawButtonAsDown ? 0.94f : (shouldDrawButtonAsHighlighted ? 1.12f : 1.0f);
    const auto highlight = shouldDrawButtonAsHighlighted ? 0.22f : 0.0f;

    layer.drawContent (g, layer.fitContent (getLocalBounds().toFloat().reduced (4.0f), scale),
                       1.0f, highlight);
}

//==============================================================================
DrumKitComponent::DrumKitComponent (const DrumifyAssets& a)
    : assets (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

const AssetLayer* DrumKitComponent::layerFor (Zone z) const
{
    switch (z)
    {
        case Zone::kick:  return &assets.kick;
        case Zone::snare: return &assets.snare;
        case Zone::hats:  return &assets.hats;
        case Zone::none:  break;
    }

    return nullptr;
}

DrumKitComponent::Zone DrumKitComponent::zoneAt (juce::Point<float> p) const
{
    const auto frame = getLocalBounds().toFloat();

    // Each drum acts as its own hit region, so the shapes match the artwork
    // exactly. Ordered so the drums win where a cymbal stand passes behind them.
    for (auto z : { Zone::kick, Zone::snare, Zone::hats })
        if (layerFor (z)->hitsContent (frame, p, DrumifyLayout::drumHoverTolerance))
            return z;

    return Zone::none;
}

bool DrumKitComponent::hitTest (int x, int y)
{
    return zoneAt ({ (float) x, (float) y }) != Zone::none;
}

void DrumKitComponent::setZone (Zone z)
{
    if (zone != z)
    {
        zone = z;
        repaint();
    }
}

void DrumKitComponent::mouseMove (const juce::MouseEvent& e) { setZone (zoneAt (e.position)); }
void DrumKitComponent::mouseExit (const juce::MouseEvent&)   { setZone (Zone::none); }

void DrumKitComponent::mouseUp (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);

    if (e.mouseWasClicked() && z != Zone::none && onZoneClicked != nullptr)
        onZoneClicked (z);
}

void DrumKitComponent::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();

    // The layers are exported already aligned, so redrawing the hovered drum on
    // top of the faded kit leaves it at full strength in its original place.
    assets.kit.drawFrame (g, frame, zone == Zone::none ? 1.0f : 0.4f);

    if (const auto* drum = layerFor (zone))
        drum->drawFrame (g, frame);

    const AssetLayer* label = &assets.labelUploadDrumhits;

    switch (zone)
    {
        case Zone::kick:  label = &assets.labelUploadKick;  break;
        case Zone::snare: label = &assets.labelUploadSnare; break;
        case Zone::hats:  label = &assets.labelUploadHat;   break;
        case Zone::none:  break;
    }

    label->drawScaledAbout (g, { (float) DrumifyLayout::kitLabelCentreX,
                                 (float) DrumifyLayout::kitLabelCentreY },
                            DrumifyLayout::labelScale);
}

//==============================================================================
SpeakerComponent::SpeakerComponent (const DrumifyAssets& a)
    : assets (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

SpeakerComponent::~SpeakerComponent()
{
    stopTimer();
}

void SpeakerComponent::setPlayingZone (Zone z)
{
    if (playingZone == z)
        return;

    playingZone = z;

    // Only poll while something is playing; there is nothing to notice otherwise.
    if (playingZone != Zone::none)
        startTimerHz (30);
    else
        stopTimer();

    repaint();
}

void SpeakerComponent::timerCallback()
{
    // The audio thread ends playback by itself when the buffer runs out, so
    // the caption has to notice that here rather than wait for a click.
    if (isPlaybackActive != nullptr && ! isPlaybackActive())
        setPlayingZone (Zone::none);
}

SpeakerComponent::Zone SpeakerComponent::zoneAt (juce::Point<float> p) const
{
    const auto frame = getLocalBounds().toFloat();

    if (assets.speakerLeft.hitsContent (frame, p, DrumifyLayout::drumHoverTolerance))
        return Zone::left;

    if (assets.speakerRight.hitsContent (frame, p, DrumifyLayout::drumHoverTolerance))
        return Zone::right;

    return Zone::none;
}

bool SpeakerComponent::hitTest (int x, int y)
{
    return zoneAt ({ (float) x, (float) y }) != Zone::none;
}

void SpeakerComponent::setZone (Zone z)
{
    if (zone != z)
    {
        zone = z;
        repaint();
    }
}

void SpeakerComponent::mouseMove (const juce::MouseEvent& e) { setZone (zoneAt (e.position)); }
void SpeakerComponent::mouseExit (const juce::MouseEvent&)   { setZone (Zone::none); }

void SpeakerComponent::mouseUp (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);

    if (e.mouseWasClicked() && z != Zone::none && onZoneClicked != nullptr)
        onZoneClicked (z);
}

void SpeakerComponent::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    const auto dimmed = zone == Zone::none ? 1.0f : 0.4f;

    // Backdrop and the speaker that is not being pointed at both fade back, so
    // the hovered one reads as picked out - same as the drum kit.
    assets.speakerBg.drawFrame (g, frame, dimmed);
    assets.speakerLeft.drawFrame  (g, frame, zone == Zone::right ? dimmed : 1.0f);
    assets.speakerRight.drawFrame (g, frame, zone == Zone::left  ? dimmed : 1.0f);

    // Off both speakers: the resting caption. On a speaker: its preview caption,
    // unless that speaker is the one currently playing, in which case the click
    // would stop it, so say so. The other speaker keeps its preview caption even
    // while one is playing - clicking it switches source rather than stops.
    const AssetLayer* label = &assets.labelPreviewAudio;

    if (zone != Zone::none)
    {
        if (zone == playingZone)
            label = &assets.labelStopPlayback;
        else
            label = zone == Zone::left ? &assets.labelPreviewInput : &assets.labelPreviewOutput;
    }

    label->drawScaledAbout (g, { (float) DrumifyLayout::speakerLabelCentreX,
                                 (float) DrumifyLayout::speakerLabelCentreY },
                            DrumifyLayout::labelScale);
}

//==============================================================================

SaveComponent::SaveComponent (const DrumifyAssets& a)
    : assets (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

SaveComponent::Zone SaveComponent::zoneAt (juce::Point<float> p) const
{
    const auto frame = getLocalBounds().toFloat();

    if (assets.saveKeyboard.hitsContent (frame, p, DrumifyLayout::drumHoverTolerance))
        return Zone::left;

    if (assets.saveLoop.hitsContent (frame, p, DrumifyLayout::drumHoverTolerance))
        return Zone::right;

    return Zone::none;
}

bool SaveComponent::hitTest (int x, int y)
{
    return zoneAt ({ (float) x, (float) y }) != Zone::none;
}

void SaveComponent::setZone (Zone z)
{
    if (zone != z)
    {
        zone = z;
        repaint();
    }
}

void SaveComponent::mouseMove (const juce::MouseEvent& e) { setZone (zoneAt (e.position)); }
void SaveComponent::mouseExit (const juce::MouseEvent&)   { setZone (Zone::none); }

void SaveComponent::mouseDown (const juce::MouseEvent& e)
{
    pressedZone = zoneAt (e.position);
    dragStarted = false;
}

void SaveComponent::mouseDrag (const juce::MouseEvent& e)
{
    // One export per gesture: the OS drag runs its own event loop, so without
    // this latch mouseDrag would re-enter and start a second drag.
    if (dragStarted || pressedZone == Zone::none || onZoneDragged == nullptr)
        return;

    if (e.getDistanceFromDragStart() < 8)
        return;

    dragStarted = true;
    onZoneDragged (pressedZone);

    // The OS drag can swallow the mouseExit, which would leave the section stuck
    // in its hovered state once the pointer has gone.
    setZone (Zone::none);
}

void SaveComponent::mouseUp (const juce::MouseEvent& e)
{
    const auto z = pressedZone;

    pressedZone = Zone::none;

    // mouseWasClicked() is already false once the pointer has travelled far
    // enough to drag, so a drag never also fires the click action.
    if (! dragStarted && e.mouseWasClicked() && z != Zone::none && onZoneClicked != nullptr)
        onZoneClicked (z);

    dragStarted = false;
}

void SaveComponent::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    const auto dimmed = zone == Zone::none ? 1.0f : 0.4f;

    // Backdrop and the button that is not being pointed at both fade back, so
    // the hovered one reads as picked out - same as the drum kit.
    assets.savePaint.drawFrame (g, frame, dimmed);
    assets.saveKeyboard.drawFrame  (g, frame, zone == Zone::right ? dimmed : 1.0f);
    assets.saveLoop.drawFrame (g, frame, zone == Zone::left ? dimmed : 1.0f);

    const AssetLayer* label = &assets.labelSaveOutput;

    if (zone == Zone::left)
        label = &assets.labelSaveMidi;
    else if (zone == Zone::right)
        label = &assets.labelSaveAudio;

    label->drawScaledAbout (g, { (float) DrumifyLayout::saveLabelCentreX,
                                 (float) DrumifyLayout::saveLabelCentreY },
                            DrumifyLayout::labelScale);
}

//==============================================================================
MicrophoneComponent::MicrophoneComponent (const DrumifyAssets& a)
    : assets (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> MicrophoneComponent::getFrame() const
{
    // The mic artwork is a square frame anchored to the bottom of the component;
    // the space above it holds the caption.
    const auto side = (float) getWidth();
    return { 0.0f, (float) getHeight() - side, side, side };
}

juce::Rectangle<float> MicrophoneComponent::getCapsule() const
{
    const auto frame = getFrame();
    const auto& back = assets.capsuleBack;

    if (! back.image.isValid() || back.content.isEmpty())
        return frame;

    const auto s = frame.getWidth() / (float) back.image.getWidth();

    return { frame.getX() + (float) back.content.getX() * s,
             frame.getY() + (float) back.content.getY() * s,
             (float) back.content.getWidth()  * s,
             (float) back.content.getHeight() * s };
}

float MicrophoneComponent::getSplitY() const
{
    const auto frame = getFrame();

    if (! assets.topRing.image.isValid())
        return getCapsule().getCentreY();

    const auto s = frame.getWidth() / (float) assets.topRing.image.getWidth();

    // The ring is where the two shells meet, so split there rather than at the
    // capsule's midpoint.
    const auto ringCentre = ((float) assets.topRing.content.getBottom()
                             + (float) assets.bottomRing.content.getY()) * 0.5f;

    return frame.getY() + ringCentre * s;
}

MicrophoneComponent::Zone MicrophoneComponent::zoneAt (juce::Point<float> p) const
{
    const auto capsule = getCapsule();

    // Elliptical test, so the corners of the capsule's bounding box stay inert.
    const auto c = capsule.getCentre();
    const auto dx = (p.x - c.x) / (capsule.getWidth()  * 0.5f);
    const auto dy = (p.y - c.y) / (capsule.getHeight() * 0.5f);

    if (dx * dx + dy * dy > 1.0f)
        return Zone::none;

    return p.y < getSplitY() ? Zone::top : Zone::bottom;
}

bool MicrophoneComponent::hitTest (int x, int y)
{
    return zoneAt ({ (float) x, (float) y }) != Zone::none;
}

void MicrophoneComponent::setZone (Zone z)
{
    if (zone != z)
    {
        zone = z;
        repaint();
    }
}

void MicrophoneComponent::setRecording (bool shouldBeRecording)
{
    if (recording != shouldBeRecording)
    {
        recording = shouldBeRecording;
        repaint();
    }
}

void MicrophoneComponent::mouseMove (const juce::MouseEvent& e) { setZone (zoneAt (e.position)); }
void MicrophoneComponent::mouseExit (const juce::MouseEvent&)   { setZone (Zone::none); }

void MicrophoneComponent::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    const auto z = zoneAt (e.position);

    if (z == Zone::top && onRecordToggled != nullptr)
        onRecordToggled();
    else if (z == Zone::bottom && onUploadLoop != nullptr)
        onUploadLoop();
}

void MicrophoneComponent::paint (juce::Graphics& g)
{
    const auto frame = getFrame();

    // While armed the capsule stays open and switches to the recording artwork,
    // so recording looks different from merely pointing at it.
    const bool openTop = (zone == Zone::top) || recording;
    const bool openBottom = (zone == Zone::bottom) && ! recording;

    assets.handle.drawFrame (g, frame);
    (recording ? assets.capsuleBackRecording : assets.capsuleBack).drawFrame (g, frame);

    if (openTop)
    {
        // Top shell removed; the bottom shell keeps the ring.
        (recording ? assets.bottomRecording : assets.bottomRing).drawFrame (g, frame);
    }
    else if (openBottom)
    {
        // Bottom shell removed; the top shell keeps the ring.
        assets.topRing.drawFrame (g, frame);
    }
    else
    {
        assets.topNoRing.drawFrame (g, frame);
        assets.bottomRing.drawFrame (g, frame);
    }

    // While armed the caption names the action if you are pointing at it, and
    // reports the state otherwise.
    const AssetLayer* label = &assets.labelInput;

    if (recording)
        label = (zone == Zone::top) ? &assets.labelStopRecording : &assets.labelRecording;
    else if (zone == Zone::top)
        label = &assets.labelRecord;
    else if (zone == Zone::bottom)
        label = &assets.labelUploadLoop;

    label->drawScaledAbout (g, { (float) getWidth() * 0.5f,
                                 ((float) getHeight() - frame.getHeight()) * 0.5f },
                            DrumifyLayout::labelScale);
}

//==============================================================================
InfoPageComponent::InfoPageComponent (const DrumifyAssets& a)
    : assets (a)
{
    body.setMultiLine (true);
    body.setReadOnly (true);
    body.setScrollbarsShown (true);
    body.setCaretVisible (false);
    body.setPopupMenuEnabled (false);
    body.setFont (DrumifyTheme::mono (24.0f));
    body.setLineSpacing (1.18f);
    body.setColour (juce::TextEditor::backgroundColourId,     juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::textColourId,           DrumifyTheme::ink);
    body.setText (infoPanelText, false);
    addAndMakeVisible (body);
}

void InfoPageComponent::paint (juce::Graphics& g)
{
    assets.about.drawFrame (g, getLocalBounds().toFloat());
}

void InfoPageComponent::resized()
{
    using namespace DrumifyLayout;
    body.setBounds (aboutBodyX, aboutBodyY, aboutBodyW, aboutBodyH);
}

bool InfoPageComponent::hitTest (int x, int y)
{
    // The page covers the whole canvas, but only the copy takes the mouse, so
    // the "?" that closes it stays clickable underneath.
    return body.getBounds().contains (x, y);
}
//==============================================================================
namespace
{
    // Everything below is in canvas coordinates, read off the SettingsMenuFull
    // reference: the ten boxes are 37x37 and the five slider slots are where
    // frame 0 of the slider strip (knob fully left) sits in that image.
    constexpr int checkBoxSize = 37;

    const juce::Point<int> checkBoxOrigins[] {
        { 107, 261 }, { 107, 313 }, { 107, 365 },                          // kicks, snares, hats
        { 449, 210 }, { 449, 338 }, { 449, 390 }, { 449, 442 }, { 449, 569 }, // quantize, host, file, select, swing
        { 876, 208 }, { 876, 336 }                                          // stretch output, musical stretch
    };

    // The strip is 221 frames of 315x31; the knob's centre runs from x=17 in
    // frame 0 to x=299 in the last, both measured from the slot's left edge,
    // and moves evenly enough between them for a linear value-to-frame map.
    constexpr int sliderFrames = 221;
    constexpr int sliderWidth = 315, sliderHeight = 31;
    constexpr float knobTravelStart = 17.0f, knobTravelEnd = 299.0f;

    const juce::Point<int> sliderOrigins[] {
        { 33, 475 }, { 450, 263 }, { 450, 496 }, { 450, 623 }, { 867, 265 }
    };

    // Where the "xx" placeholders sit under the three sliders that have one.
    const juce::Point<int> readoutCentres[] {
        { 0, 0 }, { 595, 313 }, { 595, 545 }, { 0, 0 }, { 1016, 313 }
    };

    // Right edge of each box's caption in the static artwork, for the checks
    // that can be disabled. The caption fades with its box.
    constexpr int captionRight[] { 0, 0, 0, 0, 758, 758, 662, 579, 0, 1172 };

    const char* const quantizeNames[] { "1/32", "1/16 t", "1/16", "1/8 t", "1/8", "1/4 t", "1/4" };
    constexpr double quantizeDivisions[] { 1.0 / 32, 1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4 };
    constexpr int numQuantizeSteps = 7;

    constexpr float disabledOpacity = 0.3f;
}

SettingsPageComponent::SettingsPageComponent (const DrumifyAssets& a, HackBrownAudioProcessor& p)
    : assets (a), processor (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // The three replace toggles mirror the processor rather than defaulting
    // here, so reopening the page always shows what is actually in force.
    checked[kicks]  = processor.replaceKick;
    checked[snares] = processor.replaceSnare;
    checked[hats]   = processor.replaceHat;

    // Musically sensible resting positions. Stretch must start at 1x - it is
    // wired to playbackSpeed, and ticking "Stretch output" should not warp the
    // loop until the knob is moved.
    value[sensitivity]    = 0.5f;
    value[quantizeAmount] = 2.0f / (numQuantizeSteps - 1);   // 1/16
    value[bpm]            = (120.0f - 10.0f) / 210.0f;        // 120 BPM
    value[swingAmount]    = 0.0f;
    value[stretch]        = 0.5f;                             // 1x on the log scale
}

bool SettingsPageComponent::isEnabled (Check c) const
{
    switch (c)
    {
        case lockHost: case lockFile: case selectBpm: case swing:
            return checked[quantize];
        case musicalStretch:
            return checked[stretchOutput];
        default:
            return true;
    }
}

bool SettingsPageComponent::isEnabled (Slide sl) const
{
    switch (sl)
    {
        case quantizeAmount: return checked[quantize];
        case bpm:            return checked[quantize] && checked[selectBpm];
        case swingAmount:    return checked[quantize] && checked[swing];
        case stretch:        return checked[stretchOutput];
        default:             return true;
    }
}

int SettingsPageComponent::checkAt (juce::Point<int> p) const
{
    for (int i = 0; i < numChecks; ++i)
        if (juce::Rectangle<int> (checkBoxOrigins[i], checkBoxOrigins[i].translated (checkBoxSize, checkBoxSize)).contains (p))
            return i;

    return -1;
}

int SettingsPageComponent::slideAt (juce::Point<int> p) const
{
    for (int i = 0; i < numSlides; ++i)
        if (juce::Rectangle<int> (sliderOrigins[i], sliderOrigins[i].translated (sliderWidth, sliderHeight))
              .expanded (0, 6).contains (p))
            return i;

    return -1;
}

bool SettingsPageComponent::hitTest (int x, int y)
{
    // Only live controls take the mouse, so the pointing hand and the clicks
    // both stop at anything drawn at low opacity.
    const juce::Point<int> p { x, y };

    if (const int c = checkAt (p); c >= 0)
        return isEnabled ((Check) c);

    if (const int sl = slideAt (p); sl >= 0)
        return isEnabled ((Slide) sl);

    return false;
}

void SettingsPageComponent::toggle (Check c)
{
    if (! isEnabled (c))
        return;

    // The three BPM sources are exclusive: ticking one clears the others.
    if (c == lockHost || c == lockFile || c == selectBpm)
    {
        const bool turningOn = ! checked[c];
        checked[lockHost] = checked[lockFile] = checked[selectBpm] = false;
        checked[c] = turningOn;
    }
    else
    {
        checked[c] = ! checked[c];
    }

    applyToProcessor (true);
    repaint();
}

float SettingsPageComponent::valueForX (Slide sl, int x) const
{
    const float local = (float) (x - sliderOrigins[sl].x);
    float v = juce::jlimit (0.0f, 1.0f, (local - knobTravelStart) / (knobTravelEnd - knobTravelStart));

    // The quantize slider is stepped: snap to the nearest of its seven values.
    if (sl == quantizeAmount)
        v = std::round (v * (numQuantizeSteps - 1)) / (float) (numQuantizeSteps - 1);

    return v;
}

void SettingsPageComponent::setSlider (Slide sl, float v)
{
    if (value[sl] == v)
        return;

    value[sl] = v;
    repaint();
}

void SettingsPageComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    if (const int c = checkAt (p); c >= 0)
    {
        toggle ((Check) c);
        return;
    }

    if (const int sl = slideAt (p); sl >= 0 && isEnabled ((Slide) sl))
    {
        dragging = sl;
        setSlider ((Slide) sl, valueForX ((Slide) sl, p.x));
    }
}

void SettingsPageComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
        setSlider ((Slide) dragging, valueForX ((Slide) dragging, e.getPosition().x));
}

void SettingsPageComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;

    // A slider only reaches the processor when the drag ends: re-rendering the
    // loop on every mouse-move would be far too heavy.
    const bool affectsProcessor = (dragging != sensitivity);
    dragging = -1;

    if (affectsProcessor)
        applyToProcessor (true);
}

juce::String SettingsPageComponent::readout (Slide sl) const
{
    const float v = value[sl];

    switch (sl)
    {
        case quantizeAmount:
            return quantizeNames[juce::jlimit (0, numQuantizeSteps - 1, juce::roundToInt (v * (numQuantizeSteps - 1)))];

        case bpm:
            return juce::String (juce::roundToInt (10.0f + v * 210.0f)) + " BPM";

        case stretch:
            if (checked[musicalStretch])
                return juce::String (juce::roundToInt (10.0f + v * 210.0f)) + " BPM";

            // 0.1x to 10x on a log scale, so 1x sits at the centre of the travel.
            return juce::String (0.1 * std::pow (100.0, (double) v), 2) + "x";

        default:
            return {};
    }
}

void SettingsPageComponent::applyToProcessor (bool rerender)
{
    processor.replaceKick  = checked[kicks];
    processor.replaceSnare = checked[snares];
    processor.replaceHat   = checked[hats];

    // The stretch factor is exactly playbackSpeed, which already spaces onsets
    // apart by a factor at render. Musical stretch (to a BPM) is not built yet,
    // so in that mode - or with stretching off - the loop plays unstretched.
    if (checked[stretchOutput] && ! checked[musicalStretch])
        processor.playbackSpeed = (float) (0.1 * std::pow (100.0, (double) value[stretch]));
    else
        processor.playbackSpeed = 1.0f;

    // Quantisation, converted from knob positions to real units here so the
    // processor never has to know the sliders' ranges. The BPM is the selected
    // one for now; the host and file sources have nothing to supply yet.
    processor.quantizeEnabled  = checked[quantize];
    processor.quantizeBpm      = 10.0 + (double) value[bpm] * 210.0;
    processor.quantizeDivision = quantizeDivisions[juce::jlimit (0, numQuantizeSteps - 1,
                                                                juce::roundToInt (value[quantizeAmount] * (numQuantizeSteps - 1)))];
    processor.swingAmount      = checked[swing] ? (double) value[swingAmount] : 0.0;

    if (rerender && ! processor.inputProcessor.classifiedHits.empty())
    {
        processor.isPlaybackOn.store (false);   // the render swaps the buffer playAudio reads
        processor.buildDrumBuffer();
    }
}

void SettingsPageComponent::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();

    assets.settingsMenu.drawFrame (g, frame);

    for (int i = 0; i < numChecks; ++i)
    {
        const auto box = juce::Rectangle<int> (checkBoxOrigins[i], checkBoxOrigins[i].translated (checkBoxSize, checkBoxSize));
        const float opacity = isEnabled ((Check) i) ? 1.0f : disabledOpacity;

        // The box and its caption are ink in the static artwork, already drawn
        // at full strength. Since the page is transparent, they are faded by
        // washing the background back over that region - which is the same
        // thing the pixels would look like drawn at reduced opacity.
        if (opacity < 1.0f)
        {
            const auto region = box.withRight (captionRight[i] + 4).expanded (4);
            g.setOpacity (1.0f - opacity);
            g.drawImage (assets.background.image,
                         region.getX(), region.getY(), region.getWidth(), region.getHeight(),
                         region.getX(), region.getY(), region.getWidth(), region.getHeight());
            g.setOpacity (1.0f);
        }

        if (checked[i])
            assets.checkmark.drawScaledAbout (g, box.getCentre().toFloat(), 1.0f, opacity);
    }

    for (int i = 0; i < numSlides; ++i)
    {
        const float opacity = isEnabled ((Slide) i) ? 1.0f : disabledOpacity;
        const int frameIndex = juce::jlimit (0, sliderFrames - 1, juce::roundToInt (value[i] * (sliderFrames - 1)));

        g.setOpacity (opacity);
        g.drawImage (assets.sliderStrip.image,
                     sliderOrigins[i].x, sliderOrigins[i].y, sliderWidth, sliderHeight,
                     0, frameIndex * sliderHeight, sliderWidth, sliderHeight);
        g.setOpacity (1.0f);

        const auto text = readout ((Slide) i);

        if (text.isNotEmpty())
        {
            g.setColour (DrumifyTheme::ink.withAlpha (opacity));
            g.setFont (DrumifyTheme::mono (22.0f));
            g.drawText (text, juce::Rectangle<int> (140, 30).withCentre (readoutCentres[i]),
                        juce::Justification::centred);
        }
    }
}

//==============================================================================
HackBrownAudioProcessorEditor::HackBrownAudioProcessorEditor (HackBrownAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&drumifyLookAndFeel);

    drumKit.onZoneClicked = [this] (DrumKitComponent::Zone z)
    {
        switch (z)
        {
            case DrumKitComponent::Zone::kick:  loadDrumSample (DrumType::kick);  break;
            case DrumKitComponent::Zone::snare: loadDrumSample (DrumType::snare); break;
            case DrumKitComponent::Zone::hats:  loadDrumSample (DrumType::hat);   break;
            case DrumKitComponent::Zone::none:  break;
        }
    };

    microphone.onRecordToggled = [this] { toggleRecording(); };
    // Temporarily repurposed as the batch-analysis trigger. Loading a single
    // loop is still reachable by dropping a file onto the window.
    if (true) microphone.onUploadLoop    = [this] { loadDrumLoopFromDisk(); };
    else microphone.onUploadLoop    = [this] { batchAnalyseFolder(); };

    speakers.onZoneClicked = [this] (SpeakerComponent::Zone z)
    {
        // A second click on the speaker that is playing stops it. A click on the
        // other one switches source: startPreview already halts the current
        // playback before it swaps buffers.
        if (speakers.getPlayingZone() == z)
        {
            audioProcessor.isPlaybackOn.store (false);
            speakers.setPlayingZone (SpeakerComponent::Zone::none);
            return;
        }

        audioProcessor.startPreview (z == SpeakerComponent::Zone::left ? PreviewSource::input
                                                                      : PreviewSource::output);
        speakers.setPlayingZone (z);
    };

    speakers.isPlaybackActive = [this] { return audioProcessor.isPlaybackOn.load(); };
    
    saveButtons.onZoneClicked = [this] (SaveComponent::Zone z) {
        z == SaveComponent::Zone::left ? saveMidiToDisk() : saveAudioToDisk();
    };

    saveButtons.onZoneDragged = [this] (SaveComponent::Zone z) { startDragExport (z); };

    // Scratch files from previous sessions are only safe to remove now: a host
    // that referenced rather than copied one still needed it after the drop.
    if (auto folder = getDragExportFolder(); folder.isDirectory())
        for (const auto& stale : folder.findChildFiles (juce::File::findFiles, false))
            stale.deleteFile();

    updateButton.onClick   = [this] { audioProcessor.reconstructLoopFromHits(); };
    plusButton.onClick     = [this] { toggleSettingsPage(); };
    questionButton.onClick = [this] { toggleInfoPage(); };

    formatManager.registerBasicFormats();

    addAndMakeVisible (drumKit);
    addAndMakeVisible (speakers);
    addAndMakeVisible (microphone);
    addAndMakeVisible (updateButton);
    addAndMakeVisible (plusButton);
    addAndMakeVisible (questionButton);

    // Added last so it draws over the controls as they pass each other.
    addAndMakeVisible (infoPage);
    addAndMakeVisible (settingsPage);
    addAndMakeVisible (saveButtons);

    setSize (DrumifyLayout::canvasWidth, DrumifyLayout::canvasHeight);
}

HackBrownAudioProcessorEditor::~HackBrownAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void HackBrownAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The paper backdrop stays put; everything sitting on it slides.
    assets.background.drawFrame (g, getLocalBounds().toFloat());

    // The title is canvas-aligned, like the page headings that replace it.
    assets.title.drawFrame (g, getLocalBounds().toFloat()
                                 .translated (slideProgress * (float) getWidth(), 0.0f));
}

void HackBrownAudioProcessorEditor::resized()
{
    using namespace DrumifyLayout;

    // Everything rides this offset: zero at rest, a full width right with the
    // about page open, a full width left with settings. The one exception is
    // the icon that closes the open page. Off-window bounds also stop the
    // hidden controls seeing the mouse.
    const auto shift = juce::roundToInt (slideProgress * (float) getWidth());

    // The drum layers are canvas-aligned, so this covers the whole editor and
    // relies on per-pixel hit testing to stay out of everything else's way.
    drumKit.setBounds (getLocalBounds().translated (shift, 0));
    speakers.setBounds (getLocalBounds().translated (shift, 0));
    saveButtons.setBounds (getLocalBounds().translated (shift, 0));

    microphone.setBounds (juce::Rectangle<int> (micCentreX - micFrameSize / 2, micTop,
                                                micFrameSize, canvasHeight - micTop)
                            .translated (shift, 0));

    // The about page trails a full width behind, so it arrives as the rest
    // leaves. Canvas-aligned artwork, so it is a whole page.
    infoPage.setBounds (getLocalBounds().translated (shift - getWidth(), 0));

    // The settings page waits a full width ahead and arrives from the right as
    // the controls slide left. Canvas-aligned artwork, so it is a whole page.
    settingsPage.setBounds (getLocalBounds().translated (shift + getWidth(), 0));

    // The three icons sit in a right-aligned row, each keeping its own aspect.
    const std::pair<AssetButton*, const AssetLayer*> icons[]
    {
        { &updateButton,   &assets.menuUpdate },
        { &plusButton,     &assets.menuPlus },
        { &questionButton, &assets.menuQuestion }
    };

    constexpr int padding = 6;   // room for the hover enlargement

    std::array<int, 3> widths {};
    int total = menuGap * 2;

    for (size_t i = 0; i < widths.size(); ++i)
    {
        const auto& c = icons[i].second->content;

        widths[i] = c.isEmpty() ? menuIconHeight
                                : juce::roundToInt ((float) menuIconHeight
                                                      * (float) c.getWidth() / (float) c.getHeight());
        total += widths[i];
    }

    int x = menuRight - total;

    for (size_t i = 0; i < widths.size(); ++i)
    {
        // The icon that opened a page stays put as the way back from it; the
        // other two leave with the controls. "?" holds its place while the
        // about page is in view (a positive slide), "+" while settings is.
        const bool holds = (icons[i].first == &questionButton && slideProgress > 0.0f)
                        || (icons[i].first == &plusButton     && slideProgress < 0.0f);
        const auto iconShift = holds ? 0 : shift;

        icons[i].first->setBounds (x - padding + iconShift, menuCentreY - menuIconHeight / 2 - padding,
                                   widths[i] + padding * 2, menuIconHeight + padding * 2);
        x += widths[i] + menuGap;
    }
}

//==============================================================================
void HackBrownAudioProcessorEditor::toggleRecording()
{
    const bool wasRecording = microphone.isRecording();
    const bool nowRecording = ! wasRecording;

    if (! nowRecording && audioProcessor.recordingStarted.load())
        audioProcessor.reconstructLoopFromHits();

    audioProcessor.recordingEnabled.store (nowRecording);
    microphone.setRecording (nowRecording);
}

void HackBrownAudioProcessorEditor::applyDrumSample (DrumType drum, const juce::File& file)
{
    audioProcessor.loadSampleFromFile (file, audioProcessor.drumMidiMap[drum]);

    // Must follow the load: it scans the sounds actually in the synth.
    audioProcessor.getLongestSampleLengthInSamples();
}

void HackBrownAudioProcessorEditor::loadDrumSample (DrumType drum)
{
    fileOpener ([this, drum] (const juce::File& file) { applyDrumSample (drum, file); });
}

HackBrownAudioProcessorEditor::DropTarget
HackBrownAudioProcessorEditor::dropTargetAt (juce::Point<int> editorPoint) const
{
    // Same hit regions the mouse uses, so a drop lands exactly where a click
    // would. Each component's zoneAt expects its own coordinate space.
    const auto kitPoint = drumKit.getLocalPoint (this, editorPoint).toFloat();

    switch (drumKit.zoneAt (kitPoint))
    {
        case DrumKitComponent::Zone::kick:  return DropTarget::kick;
        case DrumKitComponent::Zone::snare: return DropTarget::snare;
        case DrumKitComponent::Zone::hats:  return DropTarget::hats;
        case DrumKitComponent::Zone::none:  break;
    }

    // Everywhere else uploads the file as the loop - the original whole-window
    // behaviour, which is also exactly what a click on the capsule's lower half
    // does, so that target needs no separate case.
    return DropTarget::loop;
}

void HackBrownAudioProcessorEditor::showDragTarget (juce::Point<int> editorPoint)
{
    const auto drumZone = drumKit.zoneAt (drumKit.getLocalPoint (this, editorPoint).toFloat());

    drumKit.setZone (drumZone);

    // Off the drums the drop becomes the loop, so show the capsule exactly as
    // it looks when the mouse is on its lower half - the same action.
    microphone.setZone (drumZone == DrumKitComponent::Zone::none
                            ? MicrophoneComponent::Zone::bottom
                            : MicrophoneComponent::Zone::none);
}

void HackBrownAudioProcessorEditor::clearDragTarget()
{
    drumKit.setZone (DrumKitComponent::Zone::none);
    microphone.setZone (MicrophoneComponent::Zone::none);
}

void HackBrownAudioProcessorEditor::toggleInfoPage()
{
    infoPageVisible = ! infoPageVisible;
    settingsPageVisible = false;   // the two pages leave in opposite directions, so never both
    startTimerHz (60);
}

void HackBrownAudioProcessorEditor::toggleSettingsPage()
{
    settingsPageVisible = ! settingsPageVisible;
    infoPageVisible = false;
    startTimerHz (60);
}

void HackBrownAudioProcessorEditor::timerCallback()
{
    const auto target = infoPageVisible ? 1.0f : (settingsPageVisible ? -1.0f : 0.0f);
    const auto remaining = target - slideProgress;

    // Exponential ease-out: fast off the mark, gentle as it settles.
    if (std::abs (remaining) < 0.003f)
    {
        slideProgress = target;
        stopTimer();
    }
    else
    {
        slideProgress += remaining * 0.2f;
    }

    resized();
    repaint();
}


// Dev tool. Absolute because a plugin binary has no reliable way back to the
// repo. Matches the style of the CSV path InputProcessor's constructor opens.
static const char* const batchAnalysisRoot =
    "/Users/lightspark/Documents/JuceProjects/HackBrown2026";

namespace
{
    struct BatchTarget
    {
        const char* label;
        const char* folder;   // relative to batchAnalysisRoot
        const char* csv;      // ditto
        HitType expected;     // the folder name is the ground truth label
    };

    const BatchTarget batchTargets[]
    {
        // CSVs sit beside the sample folders in Data/, matching the path
        // InputProcessor's constructor opens.
        { "KICK",  "Data/Kicks",  "Data/kickstats.csv",  HitType::Kick  },
        { "SNARE", "Data/Snares", "Data/snarestats.csv", HitType::Snare },
        { "HAT",   "Data/Hats",   "Data/hatstats.csv",   HitType::Hat   }
    };

    // Prediction tallies, so a sweep over a labelled folder doubles as one row of
    // a confusion matrix.
    enum { predKick = 0, predSnare, predHat, predUnknown, numPredClasses };

    const char* const predictionNames[numPredClasses] { "Kick", "Snare", "Hat", "Unknown" };

    int predictionIndex (HitType type)
    {
        switch (type)
        {
            case HitType::Kick:  return predKick;
            case HitType::Snare: return predSnare;
            case HitType::Hat:   return predHat;
            default:             return predUnknown;
        }
    }

    /** A finished run, kept so every drum type can be reported together once the
        whole sweep is done rather than scattered through the per-file logging.
    */
    struct BatchResult
    {
        const char* label = nullptr;
        juce::File folder;
        juce::File csv;
        int filesProcessed = 0;
        HitClassifier::FeatureStats stats;

        int expected = predUnknown;
        std::array<int, numPredClasses> predictions {};

        int predictedTotal() const
        {
            return std::accumulate (predictions.begin(), predictions.end(), 0);
        }
    };

    /** std::cout rather than DBG so the summary survives a Release build, which
        is the one worth profiling a whole sample library with.
    */
    void printBatchStats (const char* label, const juce::File& folder,
                          const juce::File& csv, int filesProcessed,
                          const HitClassifier::FeatureStats& stats)
    {
        std::cout << "\n================ " << label << " ================\n"
                  << "Folder: " << folder.getFullPathName() << "\n"
                  << "CSV:    " << csv.getFullPathName() << "\n"
                  << "Files:  " << filesProcessed << "    Hits: " << stats.count << "\n"
                  << std::left << std::setw (20) << "Feature"
                  << std::right << std::setw (12) << "Mean"
                  << std::setw (12) << "StdDev" << "\n"
                  << std::string (44, '-') << "\n"
                  << std::fixed << std::setprecision (4);

        for (int i = 0; i < HitClassifier::numTrackedFeatures; ++i)
            std::cout << std::left << std::setw (20) << HitClassifier::trackedFeatureNames[i]
                      << std::right << std::setw (12) << stats.mean (i)
                      << std::setw (12) << stats.stdev (i) << "\n";

        std::cout << std::defaultfloat << std::endl;
    }

    /** How each labelled folder's hits were actually classified. The diagonal is
        correct predictions, so everything off it is a confusion to chase.
    */
    void printConfusionMatrix (const std::vector<BatchResult>& results)
    {
        if (results.empty())
            return;

        std::cout << "\n=============== CONFUSION MATRIX ===============\n"
                  << std::left << std::setw (10) << "Actual";

        for (int i = 0; i < numPredClasses; ++i)
            std::cout << std::right << std::setw (9) << predictionNames[i];

        std::cout << std::right << std::setw (9) << "Total"
                  << std::setw (10) << "Correct" << "\n"
                  << std::string (66, '-') << "\n";

        int totalHits = 0;
        int totalCorrect = 0;

        for (const auto& result : results)
        {
            const int hits = result.predictedTotal();
            const int correct = result.predictions[result.expected];

            totalHits += hits;
            totalCorrect += correct;

            std::cout << std::left << std::setw (10) << result.label;

            for (int i = 0; i < numPredClasses; ++i)
                std::cout << std::right << std::setw (9) << result.predictions[i];

            std::cout << std::right << std::setw (9) << hits << std::setw (9)
                      << std::fixed << std::setprecision (1)
                      << (hits > 0 ? 100.0 * correct / hits : 0.0) << "%"
                      << std::defaultfloat << "\n";
        }

        std::cout << std::string (66, '-') << "\n"
                  << "Overall accuracy: " << std::fixed << std::setprecision (1)
                  << (totalHits > 0 ? 100.0 * totalCorrect / totalHits : 0.0) << "%"
                  << "  (" << totalCorrect << "/" << totalHits << ")"
                  << std::defaultfloat << std::endl;
    }
}

void HackBrownAudioProcessorEditor::batchAnalyseFolder()
{
    const juce::File root { batchAnalysisRoot };

    juce::StringArray summary;
    std::vector<BatchResult> results;

    // Once for the whole sweep, not per file, so every folder's hits survive.
    InputProcessor::clearExtractedHits();

    for (const auto& target : batchTargets)
    {
        const auto folder = root.getChildFile (target.folder);
        const auto csv = root.getChildFile (target.csv);

        if (! folder.isDirectory())
        {
            std::cout << "\nSkipping " << target.label << ": "
                      << folder.getFullPathName() << " is not a directory." << std::endl;

            summary.add (juce::String (target.label) + ": folder missing");
            continue;
        }

        // Each drum type gets its own file, replacing any previous run's.
        audioProcessor.inputProcessor.openCsv (csv);

        if (! audioProcessor.inputProcessor.csvFile.is_open())
        {
            std::cout << "\nSkipping " << target.label << ": could not open "
                      << csv.getFullPathName() << " for writing." << std::endl;

            summary.add (juce::String (target.label) + ": CSV could not be opened");
            continue;
        }

        // Both accumulators are global, so they have to be cleared per drum type
        // or the stats would run together.
        HitClassifier::batchStats.reset();
        HitClassifier::totalFeatures = {};

        int processed = 0;
        std::array<int, numPredClasses> predictions {};

        // Recursive, so the per-category subfolders are included. This runs on
        // the message thread and the analyser is not safe to call from anywhere
        // else, so the UI will be unresponsive until every folder finishes.
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.wav", juce::File::findFiles))
        {
            audioProcessor.processUploadedLoop (entry.getFile());
            ++processed;

            // classifyStoredHits clears the vector per file, so this holds just
            // the hits found in the file that was processed above.
            for (const auto& hit : audioProcessor.inputProcessor.classifiedHits)
                ++predictions[predictionIndex (hit.type)];
        }

        // The stream is buffered, so without this the rows may not reach disk
        // until it is closed by the next target's openCsv.
        audioProcessor.inputProcessor.csvFile.flush();

        // Snapshot the accumulator: the next target resets it, and everything is
        // reported together once the whole sweep has finished.
        results.push_back ({ target.label, folder, csv, processed, HitClassifier::batchStats,
                             predictionIndex (target.expected), predictions });

        summary.add (juce::String (target.label) + ": " + juce::String (processed) + " files, "
                       + juce::String (HitClassifier::batchStats.count) + " hits -> " + csv.getFileName());
    }

    for (const auto& result : results)
        printBatchStats (result.label, result.folder, result.csv, result.filesProcessed, result.stats);

    printConfusionMatrix (results);

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                            "Batch analysis finished",
                                            summary.joinIntoString ("\n"));
}

void HackBrownAudioProcessorEditor::loadDrumLoopFromDisk()
{
    fileOpener ([this] (const juce::File& file)
    {
        audioProcessor.processUploadedLoop (file);
    });

    if (audioProcessor.inputProcessor.csvFile.is_open())
    {
        audioProcessor.inputProcessor.csvFile << "means: "
                << std::fixed << std::setprecision(4) << ","
                << HitClassifier::totalFeatures.meanCentroid << ","
                << HitClassifier::totalFeatures.centroidDelta << ","
                << HitClassifier::totalFeatures.topEndHeavyRatio << ","
                << HitClassifier::totalFeatures.lowEndHeavyRatio << ","
                << HitClassifier::totalFeatures.decayRatio << ","
                << HitClassifier::totalFeatures.deltaEnergyWeight << ","
                << HitClassifier::totalFeatures.transientZcr << ","
                << HitClassifier::totalFeatures.meanCentroidNoBass << ","
                << HitClassifier::totalFeatures.highpassZcr << std::endl;
    }
}

/** Both exports lay the loop out the same way playAudio does, so the .mid and
    the .wav line up: onsets scaled by the playback speed, shifted so the first
    hit lands at zero.
*/
static constexpr int midiTicksPerQuarterNote = 960;
static constexpr int midiMicrosecondsPerQuarter = 500000;   // 120 BPM
static constexpr int generalMidiDrumChannel = 10;

bool HackBrownAudioProcessorEditor::writeMidiTo (const juce::File& destination, bool reportFailures)
{
    const auto hits = audioProcessor.getTimedHits();

    if (hits.empty())
    {
        if (reportFailures)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Nothing to save",
                                                    "Record a rhythm or upload a loop first - there are no drum hits yet.");
        return false;
    }

    {
        const auto sampleRate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 44100.0;
        const auto speed = (double) audioProcessor.playbackSpeed;
        const auto firstOnset = (double) hits.front().onsetSample * speed;

        auto loudest = 0.0f;

        for (const auto& hit : hits)
            loudest = juce::jmax (loudest, hit.rms);

        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (midiMicrosecondsPerQuarter));

        const auto secondsToTicks = [] (double seconds)
        {
            return seconds * 1.0e6 / (double) midiMicrosecondsPerQuarter * (double) midiTicksPerQuarterNote;
        };

        for (const auto& hit : hits)
        {
            const auto startSeconds = ((double) hit.onsetSample * speed - firstOnset) / sampleRate;
            const auto lengthSeconds = juce::jmax (0.05, (double) hit.durationSec);

            const auto velocity = loudest > 0.0f ? juce::jlimit (0.2f, 1.0f, hit.rms / loudest) : 0.8f;

            // HitType's values are the General MIDI drum note numbers already.
            const auto note = (int) hit.type;

            track.addEvent (juce::MidiMessage::noteOn (generalMidiDrumChannel, note, velocity),
                            secondsToTicks (startSeconds));
            track.addEvent (juce::MidiMessage::noteOff (generalMidiDrumChannel, note),
                            secondsToTicks (startSeconds + lengthSeconds));
        }

        track.updateMatchedPairs();

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote (midiTicksPerQuarterNote);
        midiFile.addTrack (track);

        // The chooser already confirmed any overwrite, but the stream appends by
        // default so an existing file has to be truncated first.
        destination.deleteFile();

        juce::FileOutputStream stream (destination);

        if (! stream.openedOk() || ! midiFile.writeTo (stream))
        {
            if (reportFailures)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save",
                                                        "Writing to " + destination.getFullPathName() + " failed.");
            return false;
        }

        stream.flush();
    }

    return true;
}

void HackBrownAudioProcessorEditor::saveMidiToDisk()
{
    if (audioProcessor.inputProcessor.classifiedHits.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Nothing to save",
                                                "Record a rhythm or upload a loop first - there are no drum hits yet.");
        return;
    }

    fileSaver ("Save MIDI as...", "Drumify Loop.mid", ".mid", [this] (const juce::File& destination)
    {
        const auto hitCount = audioProcessor.inputProcessor.classifiedHits.size();

        if (writeMidiTo (destination, true))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                    "MIDI saved",
                                                    "Saved " + juce::String (hitCount) + " hits to "
                                                      + destination.getFullPathName());
    });
}

bool HackBrownAudioProcessorEditor::writeAudioTo (const juce::File& destination, bool reportFailures)
{
    {
        const auto& loop = audioProcessor.getRenderedLoop();

        if (loop.getNumSamples() == 0 || loop.getNumChannels() == 0)
        {
            if (reportFailures)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Nothing to save",
                                                        "Record a rhythm or upload a loop first - no audio has been generated yet.");
            return false;
        }

        const auto sampleRate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 44100.0;

        // renderDrumLoopOffline sizes the buffer for stereo but only ever writes
        // channel 0, so the file would be left-ear only. Playback hides this by
        // fanning channel 0 out to every output. Fill any silent channel from the
        // first one - guarded, so a genuinely stereo render is left untouched.
        juce::AudioBuffer<float> exportBuffer;
        exportBuffer.makeCopyOf (loop);

        for (int channel = 1; channel < exportBuffer.getNumChannels(); ++channel)
            if (exportBuffer.getMagnitude (channel, 0, exportBuffer.getNumSamples()) == 0.0f)
                exportBuffer.copyFrom (channel, 0, exportBuffer, 0, 0, exportBuffer.getNumSamples());

        // The chooser already confirmed any overwrite, but the stream appends by
        // default so an existing file has to be truncated first.
        destination.deleteFile();

        auto fileStream = std::make_unique<juce::FileOutputStream> (destination);

        if (! fileStream->openedOk())
        {
            if (reportFailures)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save",
                                                        "Could not open " + destination.getFullPathName() + " for writing.");
            return false;
        }

        // createWriterFor takes a unique_ptr<OutputStream>&, so hand it the base type.
        std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

        juce::WavAudioFormat wavFormat;

        // This overload takes the stream by reference and only moves out of it on
        // success, so a failure here leaves the stream to clean itself up.
        auto writer = wavFormat.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                            .withSampleRate (sampleRate)
                                                            .withNumChannels (exportBuffer.getNumChannels())
                                                            .withBitsPerSample (24));

        if (writer == nullptr)
        {
            if (reportFailures)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save",
                                                        "Could not create a WAV writer for " + destination.getFullPathName());
            return false;
        }

        const bool wroteOk = writer->writeFromAudioSampleBuffer (exportBuffer, 0, exportBuffer.getNumSamples());
        writer.reset();   // flushes and closes before we report success

        if (! wroteOk)
        {
            if (reportFailures)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Could not save",
                                                        "Writing to " + destination.getFullPathName() + " failed.");
            return false;
        }
    }

    return true;
}

void HackBrownAudioProcessorEditor::saveAudioToDisk()
{
    if (audioProcessor.getRenderedLoop().getNumSamples() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Nothing to save",
                                                "Record a rhythm or upload a loop first - no audio has been generated yet.");
        return;
    }

    fileSaver ("Save audio as...", "Drumify Loop.wav", ".wav", [this] (const juce::File& destination)
    {
        const auto sampleRate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 44100.0;
        const auto lengthSeconds = audioProcessor.getRenderedLoop().getNumSamples() / sampleRate;

        if (writeAudioTo (destination, true))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                    "Audio saved",
                                                    "Saved " + juce::String (lengthSeconds, 2)
                                                      + "s to " + destination.getFullPathName());
    });
}

juce::File HackBrownAudioProcessorEditor::getDragExportFolder() const
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile ("Drumify Drags");
}

void HackBrownAudioProcessorEditor::startDragExport (SaveComponent::Zone zone)
{
    if (zone == SaveComponent::Zone::none)
        return;

    const bool wantsMidi = (zone == SaveComponent::Zone::left);

    // A host is handed a path, not data, so the file has to exist before the
    // drag begins. Failures are reported here rather than inside the writer,
    // which would otherwise pop an alert in the middle of a drag gesture.
    if (wantsMidi ? audioProcessor.inputProcessor.classifiedHits.empty()
                  : audioProcessor.getRenderedLoop().getNumSamples() == 0)
        return;

    const auto folder = getDragExportFolder();

    if (! folder.createDirectory())
        return;

    // A fresh name per drag: whatever this is called becomes the clip name on
    // the host's timeline, and reusing one name across drags is confusing.
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto destination = folder.getChildFile ("Drumify " + stamp + (wantsMidi ? ".mid" : ".wav"))
                                .getNonexistentSibling();

    const bool wroteOk = wantsMidi ? writeMidiTo (destination, false)
                                   : writeAudioTo (destination, false);

    if (! wroteOk)
        return;

    // Copy, never move - the host may only reference the file, and these are
    // swept on the next editor launch rather than after the drop, because the
    // drag ending says nothing about whether the host still needs it.
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { destination.getFullPathName() }, false, this);
}

void HackBrownAudioProcessorEditor::fileSaver (const juce::String& title,
                                               const juce::String& defaultFileName,
                                               const juce::String& extension,
                                               std::function<void (const juce::File&)> saveAction)
{
    const auto startingFile = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile (defaultFileName);

    chooser = std::make_unique<juce::FileChooser> (title, startingFile, "*" + extension);

    // warnAboutOverwriting lets the native panel handle the "replace?" prompt.
    auto chooserFlags = juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (chooserFlags, [saveAction, extension] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File {})
            return;

        // Not every panel appends the extension when the user types a bare name.
        if (! file.hasFileExtension (extension))
            file = file.withFileExtension (extension);

        saveAction (file);
    });
}

//==============================================================================
bool HackBrownAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;

    juce::File file (files[0]);
    juce::String ext = file.getFileExtension().toLowerCase();
    return (ext == ".wav" || ext == ".mp3" || ext == ".aiff" || ext == ".aif" || ext == ".flac");
}

void HackBrownAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    clearDragTarget();

    if (files.isEmpty())
        return;

    const juce::File file (files[0]);

    switch (dropTargetAt ({ x, y }))
    {
        case DropTarget::kick:  applyDrumSample (DrumType::kick,  file); break;
        case DropTarget::snare: applyDrumSample (DrumType::snare, file); break;
        case DropTarget::hats:  applyDrumSample (DrumType::hat,   file); break;
        case DropTarget::loop:  audioProcessor.processUploadedLoop (file); break;
    }
}

void HackBrownAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    showDragTarget ({ x, y });
}

void HackBrownAudioProcessorEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    // setZone only repaints on a change, so this is cheap to call per move.
    showDragTarget ({ x, y });
}

void HackBrownAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    clearDragTarget();
}

void HackBrownAudioProcessorEditor::fileOpener (std::function<void (const juce::File&)> fileAction)
{
    chooser = std::make_unique<juce::FileChooser> ("Select a Wav or mp3 file to use...", juce::File {}, "*.wav;*.mp3;*.aif;*.aiff;*.flac");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this, fileAction] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file != juce::File {})
        {
            DBG ("file chosen");
            fileAction (file);
        }
    });
}
