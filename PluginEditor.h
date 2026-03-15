#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();
    void setLanguageCode(const juce::String& languageCode);
    
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider&) override;
                           
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, 
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
    
    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override;
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override;

private:
    juce::String languageCode {"en"};
};

class ShiftSlider : public juce::Slider
{
public:
    void mouseDown(const juce::MouseEvent& e) override { juce::Slider::mouseDown(swapModifiers(e)); }
    void mouseDrag(const juce::MouseEvent& e) override { juce::Slider::mouseDrag(swapModifiers(e)); }
    void mouseUp(const juce::MouseEvent& e) override { juce::Slider::mouseUp(swapModifiers(e)); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { juce::Slider::mouseDoubleClick(swapModifiers(e)); }
private:
    juce::MouseEvent swapModifiers(const juce::MouseEvent& e)
    {
        auto mods = e.mods;
        if (mods.isShiftDown())
            mods = mods.withoutFlags(juce::ModifierKeys::shiftModifier).withFlags(juce::ModifierKeys::commandModifier);
        else
            mods = mods.withoutFlags(juce::ModifierKeys::commandModifier);

        return juce::MouseEvent(e.source, e.position, mods, e.pressure, e.orientation, e.rotation,
                                e.tiltX, e.tiltY, e.eventComponent, e.originalComponent, e.eventTime,
                                e.mouseDownPosition, e.mouseDownTime, e.getNumberOfClicks(), e.mouseWasDraggedSinceMouseDown());
    }
};

class MirroredSlider : public ShiftSlider
{
public:
    double proportionOfLengthToValue(double proportion) override
    {
        return ShiftSlider::proportionOfLengthToValue(1.0 - proportion);
    }

    double valueToProportionOfLength(double value) override
    {
        return 1.0 - ShiftSlider::valueToProportionOfLength(value);
    }
};

class TitleComponent;

//==============================================================================
/**
*/
class VocalWidenerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    VocalWidenerEditor (VocalWidenerProcessor&);
    ~VocalWidenerEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void updateHaasCompVisualState(bool enabled);
    void updatePanUnitLabels(bool flipPan);
    void showSettingsPopup();
    void setSettingsVisible(bool visible);
    void setCurrentLanguageCode(const juce::String& languageCode);
    void applyLocalisation();

    VocalWidenerProcessor& audioProcessor;

    CustomLookAndFeel customLookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 700 };

    ShiftSlider offsetSlider, rightPanSlider, pitchDiffSlider, outGainSlider, haasCompAmtSlider;
    MirroredSlider leftPanSlider;
    juce::ToggleButton centeredToggle {"equal delay"}, equalPitchToggle {"equal pitch shift"}, bypassToggle {"bypass"}, linkPanToggle {"link pan"}, flipPanToggle {"flip pan"}, haasCompToggle {"haas comp"};
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attOffset, attLeftPan, attRightPan, attPitchDiff, attOutGain, attHaasAmt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attCentered, attEqualPitch, attBypass, attLinkPan, attFlipPan, attHaasEn;

    juce::Label offsetLabel, leftPanLabel, rightPanLabel, pitchDiffLabel, outGainLabel, haasCompAmtLabel;
    juce::Label offsetUnitLabel, leftPanUnitLabel, rightPanUnitLabel, pitchDiffUnitLabel, haasCompAmtUnitLabel, outGainUnitLabel;
    std::unique_ptr<TitleComponent> titleComponent;
    std::unique_ptr<juce::Component> settingsOverlay;
    juce::Label latencyLabel;
    juce::Label versionLabel;
    juce::ShapeButton settingsButton { "settings",
                                       juce::Colours::white.withAlpha(0.4f),
                                       juce::Colours::white.withAlpha(0.65f),
                                       juce::Colours::white.withAlpha(0.85f) };
    juce::TextButton resetDefaultsButton;
    void resetAllParameters();
    juce::String currentLanguageCode { "en" };
    
    // Dynamic readouts
    juce::Label leftReadout, rightReadout, haasReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalWidenerEditor)
};
