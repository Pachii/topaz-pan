#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace {
constexpr int sliderTextBoxWidth = 52;
constexpr int sliderTextBoxHeight = 20;
constexpr int unitLabelGap = 6;
constexpr int unitLabelWidth = 26;
constexpr int titleAreaHeight = 54;
constexpr float titleAreaHorizontalPadding = 4.0f;

double clampDisplayedZero(double value, double threshold) {
  return std::abs(value) < threshold ? 0.0 : value;
}

double parseNumericValue(const juce::String &text) {
  return text.retainCharacters("+-0123456789.").getDoubleValue();
}

juce::String formatOffsetMs(double value) {
  return juce::String(clampDisplayedZero(value, 0.005), 2);
}

juce::String formatPitchCents(double value) {
  return juce::String(clampDisplayedZero(value, 0.005), 2);
}

juce::String formatPanAmount(double value) {
  return juce::String(juce::roundToInt(clampDisplayedZero(value, 0.5)));
}

juce::String formatHaasPercent(double value) {
  return juce::String(juce::roundToInt(clampDisplayedZero(value, 0.5)));
}

juce::String formatOutputDb(double value) {
  return juce::String(clampDisplayedZero(value, 0.05), 1);
}

double smoothStep(double t) {
  t = juce::jlimit(0.0, 1.0, t);
  return t * t * (3.0 - 2.0 * t);
}

double mapSegment(double value, double start, double end, double outStart,
                  double outEnd, double (*easing)(double)) {
  if (end <= start)
    return outEnd;

  const double t = juce::jlimit(0.0, 1.0, (value - start) / (end - start));
  return juce::jmap(easing(t), outStart, outEnd);
}
} // namespace

class TitleComponent : public juce::Component {
public:
  explicit TitleComponent(VocalWidenerProcessor &processorRef)
      : processor(processorRef) {
    setInterceptsMouseClicks(false, false);
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    g.reduceClipRegion(bounds.getSmallestIntegerContainer());

    const auto titleState = computeTitleState(bounds);
    auto font = juce::Font("Helvetica Neue", 32.0f * titleState.scale,
                           juce::Font::bold);
    const juce::String topazWord = titleState.allCaps ? "TOPAZ" : "topaz";
    const juce::String panWord = titleState.allCaps ? "PAN" : "pan";

    const float wordTopazWidth =
        measureTrackedWord(topazWord, font, titleState.letterTracking);
    const float wordPanWidth =
        measureTrackedWord(panWord, font, titleState.letterTracking);

    const float contentWidth =
        wordTopazWidth + titleState.wordGap + wordPanWidth;
    const float startX = bounds.getX() + titleAreaHorizontalPadding;
    const float baselineY =
        bounds.getCentreY() - (font.getHeight() * 0.5f) + font.getAscent();

    const juce::Rectangle<float> clipBounds(startX, bounds.getY(), contentWidth,
                                            bounds.getHeight());
    g.reduceClipRegion(clipBounds.getSmallestIntegerContainer());

    drawWordWithEffects(g, topazWord, startX, baselineY, font, titleState);
    drawWordWithEffects(g, panWord, startX + wordTopazWidth + titleState.wordGap,
                        baselineY, font, titleState);
  }

private:
  struct TitleState {
    float wordGap = 0.0f;
    float letterTracking = 0.0f;
    float chromaOffset = 0.0f;
    float chromaAlpha = 0.0f;
    float scale = 1.0f;
    bool allCaps = false;
  };

  struct ParameterInfo {
    float min = 0.0f;
    float max = 1.0f;
    float value = 0.0f;
    float defaultValue = 0.0f;
  };

  ParameterInfo getParameterInfo(const juce::String &parameterId) const {
    auto range = processor.apvts.getParameterRange(parameterId);
    ParameterInfo info;
    info.min = range.start;
    info.max = range.end;
    info.value = juce::jlimit(info.min, info.max,
                              processor.apvts.getRawParameterValue(parameterId)
                                  ->load(std::memory_order_relaxed));

    if (auto *parameter = processor.apvts.getParameter(parameterId))
      info.defaultValue = range.convertFrom0to1(parameter->getDefaultValue());
    else
      info.defaultValue = info.value;

    return info;
  }

  TitleState computeTitleState(juce::Rectangle<float> bounds) const {
    TitleState state;

    const auto outputGain = getParameterInfo("outGain");
    const auto offset = getParameterInfo("offsetTime");
    const auto pitch = getParameterInfo("pitchDiff");
    const auto leftPanAmount = getParameterInfo("leftPan");
    const auto rightPanAmount = getParameterInfo("rightPan");
    const bool flipPan =
        processor.flipPanParam->load(std::memory_order_relaxed) > 0.5f;

    state.scale = mapOutputScale(outputGain);
    state.allCaps =
        outputGain.value >= (outputGain.max -
                             juce::jmax(0.0001f, (outputGain.max - outputGain.min) * 0.001f));
    state.letterTracking =
        mapLetterTracking(leftPanAmount.value, rightPanAmount.value, flipPan,
                          state.scale);

    auto font = juce::Font("Helvetica Neue", 32.0f * state.scale,
                           juce::Font::bold);
    const juce::String topazWord = state.allCaps ? "TOPAZ" : "topaz";
    const juce::String panWord = state.allCaps ? "PAN" : "pan";
    const float topazWidth =
        measureTrackedWord(topazWord, font, state.letterTracking);
    const float panWidth =
        measureTrackedWord(panWord, font, state.letterTracking);
    state.wordGap =
        mapWordGap(offset, font, bounds.getWidth(), topazWidth, panWidth);

    const float pitchNorm =
        pitch.max > pitch.min
            ? juce::jlimit(0.0f, 1.0f,
                           (pitch.value - pitch.min) / (pitch.max - pitch.min))
            : 0.0f;
    const float pitchEase = static_cast<float>(std::pow(pitchNorm, 0.32f));
    state.chromaOffset = 5.8f * pitchEase;
    state.chromaAlpha = 0.68f * pitchEase;

    return state;
  }

  float mapOutputScale(const ParameterInfo &outputGain) const {
    if (outputGain.value >= outputGain.defaultValue) {
      const float positiveRange =
          juce::jmax(0.001f, outputGain.max - outputGain.defaultValue);
      const float t =
          (outputGain.value - outputGain.defaultValue) / positiveRange;
      const float eased =
          static_cast<float>((0.2 * t) + (0.8 * smoothStep(t)));
      return 1.0f + 0.22f * eased;
    }

    const float negativeRange =
        juce::jmax(0.001f, outputGain.defaultValue - outputGain.min);
    const float t = (outputGain.defaultValue - outputGain.value) / negativeRange;
    return 1.0f - 0.09f * static_cast<float>(smoothStep(t));
  }

  float mapLetterTracking(float leftPanAmount, float rightPanAmount,
                          bool flipPan, float scale) const {
    juce::ignoreUnused(flipPan);

    const float leftPan = -(leftPanAmount / 100.0f);
    const float rightPan = rightPanAmount / 100.0f;
    const float spreadNorm =
        juce::jlimit(0.0f, 1.0f, std::abs(rightPan - leftPan) * 0.5f);
    const float compression =
        static_cast<float>(std::pow(1.0f - spreadNorm, 2.35f));
    return -2.35f * scale * compression;
  }

  float mapWordGap(const ParameterInfo &offset, juce::Font font, float areaWidth,
                   float topazWidth, float panWidth) const {
    const float spaceWidth =
        juce::jmax(8.0f, font.getStringWidthFloat(" ") * 0.95f);
    const float maxGap = juce::jmax(
        spaceWidth,
        (areaWidth - (2.0f * titleAreaHorizontalPadding) - topazWidth - panWidth) *
            0.97f);
    const float compressedGap = juce::jmin(spaceWidth * 0.06f, maxGap);
    const float balancedGap = juce::jmin(spaceWidth * 0.9f, maxGap);
    const float stableGap =
        juce::jmin(juce::jmax(balancedGap, balancedGap + (maxGap - balancedGap) * 0.025f),
                   maxGap);
    const float minimumGap = 0.0f;

    const double min = offset.min;
    const double max = offset.max;
    const double defaultValue = juce::jlimit(min, max, static_cast<double>(offset.defaultValue));
    const double midpoint = juce::jmap(0.5, min, defaultValue);
    const double balancedEnd =
        defaultValue + ((max - defaultValue) * 0.25);
    const double value = juce::jlimit(min, max, static_cast<double>(offset.value));

    if (value <= midpoint)
      return static_cast<float>(mapSegment(value, min, midpoint, minimumGap,
                                           compressedGap, [](double t) {
                                             return std::pow(t, 4.2);
                                           }));
    if (value <= defaultValue)
      return static_cast<float>(mapSegment(value, midpoint, defaultValue,
                                           compressedGap, balancedGap,
                                           [](double t) {
                                             return std::pow(t, 1.8);
                                           }));
    if (value <= balancedEnd)
      return static_cast<float>(mapSegment(value, defaultValue, balancedEnd,
                                           balancedGap, stableGap,
                                           [](double t) {
                                             return t * t * (3.0 - (2.0 * t));
                                           }));

    return static_cast<float>(mapSegment(value, balancedEnd, max, stableGap,
                                         maxGap, [](double t) {
                                           return (0.24 * t) +
                                                  (0.76 * std::pow(t, 2.4));
                                         }));
  }

  float measureTrackedWord(const juce::String &word, juce::Font font,
                           float tracking) const {
    float cursorX = 0.0f;
    float lastGlyphWidth = 0.0f;

    for (int i = 0; i < word.length(); ++i) {
      const juce::String glyph = juce::String::charToString(word[i]);
      const float glyphWidth = font.getStringWidthFloat(glyph);
      lastGlyphWidth = glyphWidth;

      if (i < word.length() - 1)
        cursorX += computeAdvance(glyphWidth, tracking);
    }

    return cursorX + lastGlyphWidth;
  }

  float computeAdvance(float glyphWidth, float tracking) const {
    return juce::jmax(glyphWidth * 0.58f, glyphWidth + tracking);
  }

  void drawWordWithEffects(juce::Graphics &g, const juce::String &word,
                           float x, float baselineY, juce::Font font,
                           const TitleState &state) const {
    const auto mainColour = juce::Colours::white;

    if (state.chromaAlpha > 0.0f) {
      drawTrackedWord(g, word, x - state.chromaOffset, baselineY, font,
                      juce::Colour::fromFloatRGBA(1.0f, 0.36f, 0.52f,
                                                  state.chromaAlpha),
                      state.letterTracking);
      drawTrackedWord(g, word, x + state.chromaOffset, baselineY, font,
                      juce::Colour::fromFloatRGBA(0.34f, 0.92f, 1.0f,
                                                  state.chromaAlpha),
                      state.letterTracking);
    }

    drawTrackedWord(g, word, x, baselineY, font, mainColour, state.letterTracking);
  }

  void drawTrackedWord(juce::Graphics &g, const juce::String &word, float x,
                       float baselineY, juce::Font font, juce::Colour colour,
                       float tracking) const {
    g.setColour(colour);
    g.setFont(font);

    float cursorX = x;

    for (int i = 0; i < word.length(); ++i) {
      const juce::String glyph = juce::String::charToString(word[i]);
      const float glyphWidth = font.getStringWidthFloat(glyph);
      g.drawText(glyph,
                 juce::Rectangle<float>(cursorX, baselineY - font.getAscent(),
                                        glyphWidth + 4.0f, font.getHeight()),
                 juce::Justification::centredLeft, false);

      if (i < word.length() - 1)
        cursorX += computeAdvance(glyphWidth, tracking);
    }
  }

  VocalWidenerProcessor &processor;
};

//==============================================================================
CustomLookAndFeel::CustomLookAndFeel() {
  setDefaultSansSerifTypefaceName("Helvetica Neue");
}

juce::Font CustomLookAndFeel::getLabelFont(juce::Label &) {
  return juce::Font(14.0f, juce::Font::plain);
}

juce::Label *CustomLookAndFeel::createSliderTextBox(juce::Slider &slider) {
  auto *label = juce::LookAndFeel_V4::createSliderTextBox(slider);
  label->setJustificationType(juce::Justification::centredRight);
  return label;
}

void CustomLookAndFeel::drawTooltip(juce::Graphics &g, const juce::String &text,
                                    int width, int height) {
  g.fillAll(juce::Colour::fromString("#E0222222")); // Dark subtle backdrop
  g.setColour(juce::Colours::white);
  g.setFont(14.0f);
  g.drawText(text, 0, 0, width, height, juce::Justification::centred, true);
}

void CustomLookAndFeel::drawLinearSlider(
    juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
    float /*minSliderPos*/, float /*maxSliderPos*/,
    const juce::Slider::SliderStyle /*style*/, juce::Slider &slider) {
  g.setColour(juce::Colour::fromString("#40FFFFFF"));
  float trackH = 2.0f;
  float trackY = y + height * 0.5f - trackH * 0.5f;
  g.fillRoundedRectangle(x, trackY, width, trackH, 1.0f);

  g.setColour(juce::Colours::white);
  if (slider.getProperties().getWithDefault("mirrorFill", false)) {
    float fillX = juce::jlimit(static_cast<float>(x), static_cast<float>(x + width),
                               sliderPos);
    g.fillRoundedRectangle(fillX, trackY, (x + width) - fillX, trackH, 1.0f);
  } else {
    float fillW = sliderPos - x;
    g.fillRoundedRectangle(x, trackY, fillW, trackH, 1.0f);
  }

  g.fillEllipse(sliderPos - 6.0f, y + height * 0.5f - 6.0f, 12.0f, 12.0f);
}

void CustomLookAndFeel::drawToggleButton(juce::Graphics &g,
                                         juce::ToggleButton &button,
                                         bool /*shouldDrawButtonAsHighlighted*/,
                                         bool /*shouldDrawButtonAsDown*/) {
  auto bounds = button.getLocalBounds().toFloat();

  float switchW = 32.0f;
  float switchH = 16.0f;
  float switchX = 0.0f;
  float switchY = bounds.getCentreY() - switchH * 0.5f;

  juce::Rectangle<float> switchRect(switchX, switchY, switchW, switchH);
  bool isOn = button.getToggleState();

  g.setColour(isOn ? juce::Colours::white
                   : juce::Colour::fromString("#40FFFFFF"));
  g.fillRoundedRectangle(switchRect, switchH * 0.5f);

  float thumbSize = 12.0f;
  float thumbX = isOn ? switchX + switchW - thumbSize - 2.0f : switchX + 2.0f;
  float thumbY = switchY + (switchH - thumbSize) * 0.5f;

  g.setColour(isOn ? juce::Colour::fromString("#FF7BBED4")
                   : juce::Colours::white);
  g.fillEllipse(thumbX, thumbY, thumbSize, thumbSize);

  g.setColour(juce::Colours::white);
  g.setFont(14.0f);
  g.drawText(button.getButtonText(), switchRect.getRight() + 8.0f, 0.0f,
             bounds.getWidth() - switchW - 8.0f, bounds.getHeight(),
             juce::Justification::centredLeft, true);
}

//==============================================================================
VocalWidenerEditor::VocalWidenerEditor(VocalWidenerProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setLookAndFeel(&customLookAndFeel);
  titleComponent = std::make_unique<TitleComponent>(audioProcessor);
  addAndMakeVisible(*titleComponent);
  setSize(430, 580);

  auto setupSlider = [&](juce::Slider &s, juce::Label &l,
                         const juce::String &text, auto &attachment,
                         const juce::String &paramID) {
    addAndMakeVisible(s);
    addAndMakeVisible(l);
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, sliderTextBoxWidth,
                      sliderTextBoxHeight);
    s.setColour(juce::Slider::textBoxOutlineColourId,
                juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);

    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centredLeft);
    l.setColour(juce::Label::textColourId, juce::Colours::white);

    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, paramID, s);
  };

  auto setupUnitLabel = [&](juce::Label &label, const juce::String &unitText) {
    addAndMakeVisible(label);
    label.setText(unitText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setInterceptsMouseClicks(false, false);
  };

  setupSlider(offsetSlider, offsetLabel, "offset time", attOffset,
              "offsetTime");
  offsetLabel.setTooltip("sets the delay between the left and right channels");
  offsetSlider.textFromValueFunction = [](double value) {
    return formatOffsetMs(value);
  };
  offsetSlider.valueFromTextFunction = [](const juce::String &text) {
    return parseNumericValue(text);
  };
  setupUnitLabel(offsetUnitLabel, "ms");

  setupSlider(leftPanSlider, leftPanLabel, "left pan", attLeftPan, "leftPan");
  leftPanSlider.getProperties().set("mirrorFill", true);
  leftPanLabel.setTooltip("sets how far left the left voice sits");
  leftPanSlider.textFromValueFunction = [](double value) {
    return formatPanAmount(value);
  };
  leftPanSlider.valueFromTextFunction = [](const juce::String &text) {
    return parseNumericValue(text);
  };
  setupUnitLabel(leftPanUnitLabel, "L");

  setupSlider(rightPanSlider, rightPanLabel, "right pan", attRightPan,
              "rightPan");
  rightPanLabel.setTooltip("sets how far right the right voice sits");
  rightPanSlider.textFromValueFunction = [](double value) {
    return formatPanAmount(value);
  };
  rightPanSlider.valueFromTextFunction = [](const juce::String &text) {
    return parseNumericValue(text);
  };
  setupUnitLabel(rightPanUnitLabel, "R");

  setupSlider(pitchDiffSlider, pitchDiffLabel, "pitch shift", attPitchDiff,
              "pitchDiff");
  pitchDiffLabel.setTooltip("adds subtle pitch separation between channels");
  pitchDiffSlider.textFromValueFunction = [](double value) {
    return formatPitchCents(value);
  };
  pitchDiffSlider.valueFromTextFunction = [](const juce::String &text) {
    return parseNumericValue(text);
  };
  setupUnitLabel(pitchDiffUnitLabel, "c");

  setupSlider(haasCompAmtSlider, haasCompAmtLabel, "haas comp", attHaasAmt,
              "haasCompAmt");
  haasCompAmtLabel.setTooltip(
      "balances perceived loudness when channels are delayed");
  haasCompAmtSlider.textFromValueFunction = [](double value) {
    return formatHaasPercent(value);
  };
  haasCompAmtSlider.valueFromTextFunction = [](const juce::String &text) {
    return parseNumericValue(text);
  };
  setupUnitLabel(haasCompAmtUnitLabel, "%");

  setupSlider(outGainSlider, outGainLabel, "output gain", attOutGain,
              "outGain");
  outGainLabel.setTooltip("controls the final output volume");
  outGainSlider.textFromValueFunction = [](double value) {
    return formatOutputDb(value);
  };
  outGainSlider.valueFromTextFunction = [](const juce::String &text) {
    return parseNumericValue(text);
  };
  setupUnitLabel(outGainUnitLabel, "dB");

  addAndMakeVisible(centeredToggle);
  centeredToggle.setTooltip("offsets delay equally across both channels");
  attCentered =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          audioProcessor.apvts, "centeredTiming", centeredToggle);

  addAndMakeVisible(bypassToggle);
  attBypass =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          audioProcessor.apvts, "bypass", bypassToggle);

  addAndMakeVisible(linkPanToggle);
  linkPanToggle.setTooltip("locks both pan amounts together");
  attLinkPan =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          audioProcessor.apvts, "linkPan", linkPanToggle);

  addAndMakeVisible(flipPanToggle);
  flipPanToggle.setTooltip("swaps the left and right pan destinations");
  attFlipPan =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          audioProcessor.apvts, "flipPan", flipPanToggle);

  addAndMakeVisible(haasCompToggle);
  attHaasEn =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          audioProcessor.apvts, "haasCompEn", haasCompToggle);

  addAndMakeVisible(leftReadout);
  addAndMakeVisible(rightReadout);
  addAndMakeVisible(haasReadout);

  leftReadout.setJustificationType(juce::Justification::centredLeft);
  rightReadout.setJustificationType(juce::Justification::centredLeft);
  haasReadout.setJustificationType(juce::Justification::centredLeft);

  leftReadout.setColour(juce::Label::textColourId, juce::Colours::white);
  rightReadout.setColour(juce::Label::textColourId, juce::Colours::white);
  haasReadout.setColour(juce::Label::textColourId, juce::Colours::white);

  updateHaasCompVisualState(
      audioProcessor.haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f &&
      audioProcessor.linkPanParam->load(std::memory_order_relaxed) > 0.5f);
  updatePanUnitLabels(
      audioProcessor.flipPanParam->load(std::memory_order_relaxed) > 0.5f);

  startTimerHz(30);
}

VocalWidenerEditor::~VocalWidenerEditor() { setLookAndFeel(nullptr); }

void VocalWidenerEditor::updateHaasCompVisualState(bool enabled) {
  const float activeAlpha = 1.0f;
  const float disabledAlpha = 0.4f;
  const float alpha = enabled ? activeAlpha : disabledAlpha;

  haasCompAmtSlider.setEnabled(enabled);
  haasCompAmtSlider.setAlpha(alpha);
  haasCompAmtLabel.setAlpha(alpha);
  haasCompAmtUnitLabel.setAlpha(alpha);
}

void VocalWidenerEditor::updatePanUnitLabels(bool flipPan) {
  leftPanUnitLabel.setText(flipPan ? "R" : "L", juce::dontSendNotification);
  rightPanUnitLabel.setText(flipPan ? "L" : "R", juce::dontSendNotification);
}

void VocalWidenerEditor::timerCallback() {
  if (!audioProcessor.isStereoLayout)
    return; // Banner will cover UI

  const bool haasEnabled =
      audioProcessor.haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f;
  const bool linkPanEnabled =
      audioProcessor.linkPanParam->load(std::memory_order_relaxed) > 0.5f;
  const bool effectiveHaasEnabled = haasEnabled && linkPanEnabled;
  const bool flipPan =
      audioProcessor.flipPanParam->load(std::memory_order_relaxed) > 0.5f;

  updateHaasCompVisualState(effectiveHaasEnabled);
  updatePanUnitLabels(flipPan);
  titleComponent->repaint();

  float oDel = audioProcessor.leftDelayReadout.load(std::memory_order_relaxed);
  float dDel = audioProcessor.rightDelayReadout.load(std::memory_order_relaxed);
  float oPit = audioProcessor.leftPitchReadout.load(std::memory_order_relaxed);
  float dPit = audioProcessor.rightPitchReadout.load(std::memory_order_relaxed);

  if (std::abs(oDel) < 0.05f)
    oDel = 0.0f;
  if (std::abs(dDel) < 0.05f)
    dDel = 0.0f;
  if (std::abs(oPit) < 0.05f)
    oPit = 0.0f;
  if (std::abs(dPit) < 0.05f)
    dPit = 0.0f;

  leftReadout.setText(
      juce::String::formatted("left channel:\ndelay: %+.2f ms\npitch: %+.2f c",
                              oDel, oPit),
      juce::dontSendNotification);
  rightReadout.setText(
      juce::String::formatted("right channel:\ndelay: %+.2f ms\npitch: %+.2f c",
                              dDel, dPit),
      juce::dontSendNotification);

  float earlierPath =
      audioProcessor.earlierPathReadout.load(std::memory_order_relaxed);
  float oComp = audioProcessor.leftCompReadout.load(std::memory_order_relaxed);
  float dComp = audioProcessor.rightCompReadout.load(std::memory_order_relaxed);

  if (std::abs(oComp) < 0.05f)
    oComp = 0.0f;
  if (std::abs(dComp) < 0.05f)
    dComp = 0.0f;

  const bool hasDirectionalPrecedence =
      earlierPath == 0.0f || earlierPath == 1.0f;
  const bool hasAudibleHaasGain = oComp != 0.0f || dComp != 0.0f;

  juce::String precStr = "haas precedence: ";
  if (!effectiveHaasEnabled)
    precStr += "off";
  else if (!hasDirectionalPrecedence && !hasAudibleHaasGain)
    precStr += "none";
  else if (earlierPath == 0.0f)
    precStr += "left";
  else if (earlierPath == 1.0f)
    precStr += "right";
  else
    precStr += "ambiguous";

  auto formatCompDb = [](float v) {
    return juce::String(std::abs(v) < 0.005f ? 0.0f : v, 2);
  };
  precStr += "\nleft gain: " + formatCompDb(oComp) +
             " dB   |   right gain: " + formatCompDb(dComp) + " dB";
  haasReadout.setText(precStr, juce::dontSendNotification);
}

void VocalWidenerEditor::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour::fromString("#FF7BBED4"));

  if (!audioProcessor.isBusesLayoutSupported(audioProcessor.getBusesLayout())) {
    g.setColour(juce::Colours::red.withAlpha(0.8f));
    g.fillRect(0, 0, getWidth(), 30);
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(
        "Warning: Plugin requires a Stereo track layout to function properly.",
        0, 0, getWidth(), 30, juce::Justification::centred, true);
  }

  g.setColour(juce::Colours::white.withAlpha(0.5f));
  g.setFont(juce::Font("Helvetica Neue", 12.0f, juce::Font::plain));
  g.drawText("0.1.0-alpha", getWidth() - 110, getHeight() - 30, 100, 20,
             juce::Justification::right, true);
}

void VocalWidenerEditor::resized() {
  if (titleComponent != nullptr)
    titleComponent->setBounds(30, 20, getWidth() - 60, titleAreaHeight);

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
  leftPanUnitLabel.setBounds(unitX, yStart, unitLabelWidth, rowH);
  yStart += rowH;

  rightPanLabel.setBounds(leftMargin, yStart, labelW, rowH);
  rightPanSlider.setBounds(leftMargin + labelW, yStart, sliderW, rowH);
  rightPanUnitLabel.setBounds(unitX, yStart, unitLabelWidth, rowH);
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

  flipPanToggle.setBounds(leftMargin, yStart, colWidth, colH);
  haasCompToggle.setBounds(leftMargin + colWidth + 20, yStart, colWidth, colH);
  yStart += colH;

  bypassToggle.setBounds(leftMargin, yStart, colWidth, colH);
  yStart += colH + 20;

  leftReadout.setBounds(leftMargin, yStart, 180, 50);
  rightReadout.setBounds(leftMargin + 180, yStart, 180, 50);
  yStart += 55;

  haasReadout.setBounds(leftMargin, yStart, 360, 40);
}
