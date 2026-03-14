#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();
    
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider&) override;
                           
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, 
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
                           
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    
    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override;
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
    void resized() override;
    void timerCallback() override;

private:
    void updateHaasCompVisualState(bool enabled);
    void updatePanUnitLabels(bool flipPan);

    VocalWidenerProcessor& audioProcessor;

    CustomLookAndFeel customLookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 700 };

    ShiftSlider offsetSlider, rightPanSlider, pitchDiffSlider, outGainSlider, haasCompAmtSlider;
    MirroredSlider leftPanSlider;
    juce::ToggleButton centeredToggle {"equal delay"}, bypassToggle {"bypass"}, linkPanToggle {"link pan"}, flipPanToggle {"flip pan"}, haasCompToggle {"haas comp"};
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attOffset, attLeftPan, attRightPan, attPitchDiff, attOutGain, attHaasAmt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attCentered, attBypass, attLinkPan, attFlipPan, attHaasEn;

    juce::Label offsetLabel, leftPanLabel, rightPanLabel, pitchDiffLabel, outGainLabel, haasCompAmtLabel;
    juce::Label offsetUnitLabel, leftPanUnitLabel, rightPanUnitLabel, pitchDiffUnitLabel, haasCompAmtUnitLabel, outGainUnitLabel;
    
    // Dynamic readouts
    juce::Label leftReadout, rightReadout, haasReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalWidenerEditor)
};
