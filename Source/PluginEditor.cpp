/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "hitClassifier.h"

//==============================================================================
// The copy shown by the "?" button in the top row. Replace this with the real
// text - it is the only thing the info popup renders.
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

    savePaint.load       (BinaryData::SavePaint_png,       BinaryData::SavePaint_pngSize);
    saveKeyboard.load    (BinaryData::SaveKeyboard_png,    BinaryData::SaveKeyboard_pngSize);
    saveLoop.load        (BinaryData::SaveLoop_png,        BinaryData::SaveLoop_pngSize);
    labelSaveMidi.load   (BinaryData::SaveMIDI_png,        BinaryData::SaveMIDI_pngSize);
    labelSaveAudio.load  (BinaryData::SaveAudio_png,       BinaryData::SaveAudio_pngSize);
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

    const AssetLayer* label = &assets.labelPreviewAudio;

    if (zone == Zone::left)
        label = &assets.labelPreviewInput;
    else if (zone == Zone::right)
        label = &assets.labelPreviewOutput;

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
InfoPageComponent::InfoPageComponent()
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
    g.setColour (DrumifyTheme::ink);
    g.setFont (DrumifyTheme::mono (32.0f, true));
    g.drawText ("About Drumify", getLocalBounds().removeFromTop (46),
                juce::Justification::centred);
}

void InfoPageComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (58);
    body.setBounds (area);
}

//==============================================================================
/** The contents of the "+" popup. Add further options here. */
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (HackBrownAudioProcessor& p)
        : processor (p)
    {
        speedLabel.setText ("Playback speed", juce::dontSendNotification);
        speedLabel.setFont (DrumifyTheme::mono (13.0f));
        addAndMakeVisible (speedLabel);

        speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        speedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        speedSlider.setRange (0.25, 2.0, 0.01);
        speedSlider.setValue (processor.playbackSpeed, juce::dontSendNotification);
        speedSlider.onValueChange = [this] { processor.playbackSpeed = (float) speedSlider.getValue(); };
        addAndMakeVisible (speedSlider);

        previewButton.onClick = [this]
        {
            processor.inputProcessor.reset();
            processor.isPlaybackOn.store (true);
        };
        addAndMakeVisible (previewButton);

        stopButton.onClick = [this] { processor.isPlaybackOn.store (false); };
        addAndMakeVisible (stopButton);

        setSize (300, 220);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (DrumifyTheme::ink);
        g.setFont (DrumifyTheme::mono (18.0f, true));
        g.drawText ("Settings", getLocalBounds().removeFromTop (30),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (36);

        speedLabel.setBounds (area.removeFromTop (20));
        speedSlider.setBounds (area.removeFromTop (28));
        area.removeFromTop (14);

        previewButton.setBounds (area.removeFromTop (44));
        area.removeFromTop (4);
        stopButton.setBounds (area.removeFromTop (44));
    }

private:
    HackBrownAudioProcessor& processor;

    juce::Label      speedLabel;
    juce::Slider     speedSlider;
    juce::TextButton previewButton { "Preview Audio" };
    juce::TextButton stopButton    { "Stop Playback" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

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
    microphone.onUploadLoop    = [this] { loadDrumLoopFromDisk(); };
    //microphone.onUploadLoop    = [this] { batchAnalyseFolder(); };

    speakers.onZoneClicked = [this] (SpeakerComponent::Zone z)
    {
        audioProcessor.startPreview (z == SpeakerComponent::Zone::left ? PreviewSource::input
                                                                      : PreviewSource::output);
    };
    
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
    plusButton.onClick     = [this] { showSettingsPopup(); };
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

    assets.title.drawContent (g, juce::Rectangle<float> ((float) DrumifyLayout::titleX,
                                                         (float) DrumifyLayout::titleY,
                                                         (float) DrumifyLayout::titleW,
                                                         (float) DrumifyLayout::titleH)
                                   .translated (slideProgress * (float) getWidth(), 0.0f));

    if (isDragging)
    {
        g.setColour (DrumifyTheme::recordAccent.withAlpha (0.14f));
        g.fillAll();

        g.setColour (DrumifyTheme::recordAccent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (6.0f), 14.0f, 3.0f);

        g.setFont (DrumifyTheme::mono (22.0f, true));
        g.setColour (DrumifyTheme::ink);
        g.drawText ("Drop your audio loop here", getLocalBounds(), juce::Justification::centred);
    }
}

void HackBrownAudioProcessorEditor::resized()
{
    using namespace DrumifyLayout;

    // Everything except the "?" rides this offset: at rest it is zero, and when
    // the about page is open the controls have slid a full width to the right.
    // Off-window bounds also stop the hidden controls seeing the mouse.
    const auto shift = juce::roundToInt (slideProgress * (float) getWidth());

    // The drum layers are canvas-aligned, so this covers the whole editor and
    // relies on per-pixel hit testing to stay out of everything else's way.
    drumKit.setBounds (getLocalBounds().translated (shift, 0));
    speakers.setBounds (getLocalBounds().translated (shift, 0));
    saveButtons.setBounds (getLocalBounds().translated (shift, 0));

    microphone.setBounds (juce::Rectangle<int> (micCentreX - micFrameSize / 2, micTop,
                                                micFrameSize, canvasHeight - micTop)
                            .translated (shift, 0));

    // The about page trails a full width behind, so it arrives as the rest leaves.
    infoPage.setBounds (juce::Rectangle<int> (infoPageX, infoPageY, infoPageW, infoPageH)
                          .translated (shift - getWidth(), 0));

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
        // The "?" stays put while the rest slides away - it is the way back.
        const auto iconShift = (icons[i].first == &questionButton) ? 0 : shift;

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

void HackBrownAudioProcessorEditor::loadDrumSample (DrumType drum)
{
    fileOpener ([this, drum] (const juce::File& file)
    {
        audioProcessor.loadSampleFromFile (file, audioProcessor.drumMidiMap[drum]);
    });

    audioProcessor.getLongestSampleLengthInSamples();
}

void HackBrownAudioProcessorEditor::toggleInfoPage()
{
    infoPageVisible = ! infoPageVisible;
    startTimerHz (60);
}

void HackBrownAudioProcessorEditor::timerCallback()
{
    const auto target = infoPageVisible ? 1.0f : 0.0f;
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

void HackBrownAudioProcessorEditor::showSettingsPopup()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<SettingsPanel> (audioProcessor),
                                            plusButton.getBounds(), this);
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
                << HitClassifier::totalFeatures.transientZcr << std::endl;
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
    const auto& hits = audioProcessor.inputProcessor.classifiedHits;

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

void HackBrownAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    isDragging = false;
    repaint();

    if (files.isEmpty())
        return;

    audioProcessor.processUploadedLoop (juce::File (files[0]));
}

void HackBrownAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    isDragging = true;
    repaint();
}

void HackBrownAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    isDragging = false;
    repaint();
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
