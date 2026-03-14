#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr int sliderTextBoxWidth = 52;
constexpr int sliderTextBoxHeight = 20;
constexpr int unitLabelGap = 6;
constexpr int unitLabelWidth = 26;

double clampDisplayedZero(double value, double threshold)
{
    return std::abs(value) < threshold ? 0.0 : value;
}

double parseNumericValue(const juce::String& text)
{
    return text.retainCharacters("+-0123456789.").getDoubleValue();
}

juce::String formatOffsetMs(double value)
{
    return juce::String(clampDisplayedZero(value, 0.005), 2);
}

juce::String formatPitchCents(double value)
{
    return juce::String(clampDisplayedZero(value, 0.005), 2);
}

juce::String formatHaasPercent(double value)
{
    return juce::String(juce::roundToInt(clampDisplayedZero(value, 0.5)));
}

juce::String formatOutputDb(double value)
{
    return juce::String(clampDisplayedZero(value, 0.05), 1);
}
}

//==============================================================================
CustomLookAndFeel::CustomLookAndFeel()
{
    setDefaultSansSerifTypefaceName("Helvetica Neue");
}

juce::Font CustomLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(14.0f, juce::Font::plain);
}

juce::Label* CustomLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setJustificationType(juce::Justification::centredRight);
    return label;
}

void CustomLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll(juce::Colour::fromString("#E0222222")); // Dark subtle backdrop
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(text, 0, 0, width, height, juce::Justification::centred, true);
}

void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                         const juce::Slider::SliderStyle /*style*/, juce::Slider& /*slider*/)
{
    g.setColour(juce::Colour::fromString("#40FFFFFF"));
    float trackH = 2.0f;
    float trackY = y + height * 0.5f - trackH * 0.5f;
    g.fillRoundedRectangle(x, trackY, width, trackH, 1.0f);

    float fillW = sliderPos - x;
    g.setColour(juce::Colours::white);
    g.fillRoundedRectangle(x, trackY, fillW, trackH, 1.0f);

    g.fillEllipse(sliderPos - 6.0f, y + height * 0.5f - 6.0f, 12.0f, 12.0f);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, 
                                         bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat();
    
    float switchW = 32.0f;
    float switchH = 16.0f;
    float switchX = 0.0f;
    float switchY = bounds.getCentreY() - switchH * 0.5f;
    
    juce::Rectangle<float> switchRect(switchX, switchY, switchW, switchH);
    bool isOn = button.getToggleState();
    
    g.setColour(isOn ? juce::Colours::white : juce::Colour::fromString("#40FFFFFF"));
    g.fillRoundedRectangle(switchRect, switchH * 0.5f);
    
    float thumbSize = 12.0f;
    float thumbX = isOn ? switchX + switchW - thumbSize - 2.0f : switchX + 2.0f;
    float thumbY = switchY + (switchH - thumbSize) * 0.5f;
    
    g.setColour(isOn ? juce::Colour::fromString("#FF7BBED4") : juce::Colours::white);
    g.fillEllipse(thumbX, thumbY, thumbSize, thumbSize);
    
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(button.getButtonText(), switchRect.getRight() + 8.0f, 0.0f, 
               bounds.getWidth() - switchW - 8.0f, bounds.getHeight(), 
               juce::Justification::centredLeft, true);
}

//==============================================================================
VocalWidenerEditor::VocalWidenerEditor (VocalWidenerProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&customLookAndFeel);
    setSize (430, 580);

    auto setupSlider = [&](juce::Slider& s, juce::Label& l, const juce::String& text, auto& attachment, const juce::String& paramID) {
        addAndMakeVisible(s);
        addAndMakeVisible(l);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, sliderTextBoxWidth, sliderTextBoxHeight);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredLeft);
        l.setColour(juce::Label::textColourId, juce::Colours::white);
        
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, paramID, s);
    };

    auto setupUnitLabel = [&](juce::Label& label, const juce::String& unitText) {
        addAndMakeVisible(label);
        label.setText(unitText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setInterceptsMouseClicks(false, false);
    };

    setupSlider(offsetSlider, offsetLabel, "offset time", attOffset, "offsetTime");
    offsetSlider.setTooltip("sets the delay between the left and right channels");
    offsetSlider.textFromValueFunction = [](double value) { return formatOffsetMs(value); };
    offsetSlider.valueFromTextFunction = [](const juce::String& text) { return parseNumericValue(text); };
    setupUnitLabel(offsetUnitLabel, "ms");

    setupSlider(leftPanSlider, leftPanLabel, "left pan", attLeftPan, "leftPan");
    leftPanSlider.setTooltip("pans the left channel in the stereo field");

    setupSlider(rightPanSlider, rightPanLabel, "right pan", attRightPan, "rightPan");
    rightPanSlider.setTooltip("pans the right channel in the stereo field");

    setupSlider(pitchDiffSlider, pitchDiffLabel, "pitch difference", attPitchDiff, "pitchDiff");
    pitchDiffSlider.setTooltip("adds subtle pitch separation between channels");
    pitchDiffSlider.textFromValueFunction = [](double value) { return formatPitchCents(value); };
    pitchDiffSlider.valueFromTextFunction = [](const juce::String& text) { return parseNumericValue(text); };
    setupUnitLabel(pitchDiffUnitLabel, "c");

    setupSlider(haasCompAmtSlider, haasCompAmtLabel, "haas comp", attHaasAmt, "haasCompAmt");
    haasCompAmtSlider.setTooltip("balances perceived loudness when channels are delayed");
    haasCompAmtSlider.textFromValueFunction = [](double value) { return formatHaasPercent(value); };
    haasCompAmtSlider.valueFromTextFunction = [](const juce::String& text) { return parseNumericValue(text); };
    setupUnitLabel(haasCompAmtUnitLabel, "%");

    setupSlider(outGainSlider, outGainLabel, "output gain", attOutGain, "outGain");
    outGainSlider.setTooltip("controls the final output volume");
    outGainSlider.textFromValueFunction = [](double value) { return formatOutputDb(value); };
    outGainSlider.valueFromTextFunction = [](const juce::String& text) { return parseNumericValue(text); };
    setupUnitLabel(outGainUnitLabel, "dB");

    addAndMakeVisible(centeredToggle);
    centeredToggle.setTooltip("offsets delay equally across both channels");
    attCentered = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "centeredTiming", centeredToggle);

    addAndMakeVisible(bypassToggle);
    attBypass = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "bypass", bypassToggle);

    addAndMakeVisible(linkPanToggle);
    attLinkPan = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "linkPan", linkPanToggle);

    addAndMakeVisible(haasCompToggle);
    attHaasEn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "haasCompEn", haasCompToggle);

    addAndMakeVisible(leftReadout);
    addAndMakeVisible(rightReadout);
    addAndMakeVisible(haasReadout);
    
    leftReadout.setJustificationType(juce::Justification::centredLeft);
    rightReadout.setJustificationType(juce::Justification::centredLeft);
    haasReadout.setJustificationType(juce::Justification::centredLeft);
    
    leftReadout.setColour(juce::Label::textColourId, juce::Colours::white);
    rightReadout.setColour(juce::Label::textColourId, juce::Colours::white);
    haasReadout.setColour(juce::Label::textColourId, juce::Colours::white);

    startTimerHz(30);
}

VocalWidenerEditor::~VocalWidenerEditor()
{
    setLookAndFeel (nullptr);
}

void VocalWidenerEditor::timerCallback()
{
    if (!audioProcessor.isStereoLayout) return; // Banner will cover UI

    float oDel = audioProcessor.leftDelayReadout.load(std::memory_order_relaxed);
    float dDel = audioProcessor.rightDelayReadout.load(std::memory_order_relaxed);
    float oPit = audioProcessor.leftPitchReadout.load(std::memory_order_relaxed);
    float dPit = audioProcessor.rightPitchReadout.load(std::memory_order_relaxed);

    if (std::abs(oDel) < 0.05f) oDel = 0.0f;
    if (std::abs(dDel) < 0.05f) dDel = 0.0f;
    if (std::abs(oPit) < 0.05f) oPit = 0.0f;
    if (std::abs(dPit) < 0.05f) dPit = 0.0f;

    leftReadout.setText(juce::String::formatted("left channel:\ndelay: %+.2f ms\npitch: %.2f c", oDel, oPit), juce::dontSendNotification);
    rightReadout.setText(juce::String::formatted("right channel:\ndelay: %+.2f ms\npitch: %.2f c", dDel, dPit), juce::dontSendNotification);
    
    float earlierPath = audioProcessor.earlierPathReadout.load(std::memory_order_relaxed);
    float oComp = audioProcessor.leftCompReadout.load(std::memory_order_relaxed);
    float dComp = audioProcessor.rightCompReadout.load(std::memory_order_relaxed);

    if (std::abs(oComp) < 0.05f) oComp = 0.0f;
    if (std::abs(dComp) < 0.05f) dComp = 0.0f;

    juce::String precStr = "haas precedence: ";
    if (earlierPath == 0.0f)      precStr += "left";
    else if (earlierPath == 1.0f) precStr += "right";
    else                          precStr += "ambiguous";
    
    precStr += juce::String::formatted("\nleft gain: %.1f dB   |   right gain: %.1f dB", oComp, dComp);
    haasReadout.setText(precStr, juce::dontSendNotification);
}

void VocalWidenerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromString ("#FF7BBED4"));

    if (! audioProcessor.isBusesLayoutSupported(audioProcessor.getBusesLayout()))
    {
        g.setColour (juce::Colours::red.withAlpha(0.8f));
        g.fillRect (0, 0, getWidth(), 30);
        g.setColour (juce::Colours::white);
        g.setFont (14.0f);
        g.drawText ("Warning: Plugin requires a Stereo track layout to function properly.", 
                    0, 0, getWidth(), 30, juce::Justification::centred, true);
    }
    
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font ("Helvetica Neue", 32.0f, juce::Font::bold));
    g.drawText ("topaz pan", 30, 25, getWidth() - 60, 40, juce::Justification::left, true);
    
    g.setColour (juce::Colours::white.withAlpha(0.5f));
    g.setFont (juce::Font ("Helvetica Neue", 12.0f, juce::Font::plain));
    g.drawText ("0.1 alpha", getWidth() - 90, getHeight() - 30, 80, 20, juce::Justification::right, true);
}

void VocalWidenerEditor::resized()
{
    int yStart = 90;
    int labelW = 100;
    int sliderW = 210;
    int rowH = 34;
    int leftMargin = 30;
    int unitX = leftMargin + labelW + sliderW + unitLabelGap;

    offsetLabel.setBounds(leftMargin, yStart, labelW, rowH);
    offsetSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
    offsetUnitLabel.setBounds(unitX, yStart, unitLabelWidth, rowH);
    yStart += rowH;

    leftPanLabel.setBounds(leftMargin, yStart, labelW, rowH);
    leftPanSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
    yStart += rowH;

    rightPanLabel.setBounds(leftMargin, yStart, labelW, rowH);
    rightPanSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
    yStart += rowH;

    pitchDiffLabel.setBounds(leftMargin, yStart, labelW, rowH);
    pitchDiffSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
    pitchDiffUnitLabel.setBounds(unitX, yStart, unitLabelWidth, rowH);
    yStart += rowH;

    haasCompAmtLabel.setBounds(leftMargin, yStart, labelW, rowH);
    haasCompAmtSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
    haasCompAmtUnitLabel.setBounds(unitX, yStart, unitLabelWidth, rowH);
    yStart += rowH;

    outGainLabel.setBounds(leftMargin, yStart, labelW, rowH);
    outGainSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
    outGainUnitLabel.setBounds(unitX, yStart, unitLabelWidth, rowH);
    yStart += rowH + 15;

    int colH = 30;
    int colWidth = 160;
    
    centeredToggle.setBounds(leftMargin, yStart, colWidth, colH);
    linkPanToggle.setBounds(leftMargin + colWidth + 20, yStart, colWidth, colH);
    yStart += colH;

    haasCompToggle.setBounds(leftMargin, yStart, colWidth, colH);
    bypassToggle.setBounds(leftMargin + colWidth + 20, yStart, colWidth, colH);
    yStart += colH + 20;

    leftReadout.setBounds(leftMargin, yStart, 180, 50);
    rightReadout.setBounds(leftMargin + 180, yStart, 180, 50);
    yStart += 55;

    haasReadout.setBounds(leftMargin, yStart, 360, 40);
}
