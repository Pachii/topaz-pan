#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <thread>

namespace {
constexpr int sliderTextBoxWidth = 52;
constexpr int sliderTextBoxHeight = 20;
constexpr int unitLabelGap = 6;
constexpr int unitLabelWidth = 34;
constexpr int titleAreaHeight = 54;
constexpr float titleAreaHorizontalPadding = 4.0f;

juce::Font makeHelveticaFont(float height,
                             int styleFlags = juce::Font::plain) {
  const bool bold = (styleFlags & juce::Font::bold) != 0;
#if JUCE_MAC
  return juce::Font(juce::FontOptions(bold ? "Hiragino Sans W7"
                                           : "Hiragino Sans W5",
                                      height, styleFlags));
#elif JUCE_WINDOWS
  return juce::Font(juce::FontOptions(bold ? "Yu Gothic UI Bold"
                                           : "Yu Gothic UI Semibold",
                                      height, styleFlags));
#else
  return juce::Font(juce::FontOptions(bold ? "Noto Sans CJK JP Bold"
                                           : "Noto Sans CJK JP Medium",
                                      height, styleFlags));
#endif
}

juce::Font makeMultilingualSansFont(float height,
                                    int styleFlags = juce::Font::plain) {
  return makeHelveticaFont(height, styleFlags);
}

bool isJapaneseLanguageCode(const juce::String &languageCode) {
  return VocalWidenerProcessor::normaliseLanguageCode(languageCode) == "ja";
}

float measureTextWidth(const juce::Font &font, const juce::String &text) {
  return juce::GlyphArrangement::getStringWidth(font, text);
}

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

float readoutFontHeightForLanguage(const juce::String &languageCode) {
  return isJapaneseLanguageCode(languageCode) ? 14.0f : 13.2f;
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

juce::String normaliseVersionTag(juce::String version) {
  auto normalised = version.trim().toLowerCase();

  if (normalised.isEmpty())
    return {};

  if (!normalised.startsWithChar('v'))
    normalised = "v" + normalised;

  return normalised;
}

bool isUpdateAvailable(const juce::String &currentVersion,
                       const juce::String &latestVersion) {
  const auto currentTag = normaliseVersionTag(currentVersion);
  const auto latestTag = normaliseVersionTag(latestVersion);
  return latestTag.isNotEmpty() && latestTag != currentTag;
}

enum class UILanguage { english, japanese };

struct LocalizedStrings {
  juce::String titleLeadingWord;
  juce::String titleTrailingWord;
  juce::String titleBypassTrailingWord;
  juce::String settingsTitle;
  bool allowTitleAllCaps = true;

  juce::String offsetTime;
  juce::String leftPan;
  juce::String rightPan;
  juce::String pitchShift;
  juce::String haasComp;
  juce::String outputGain;
  juce::String equalDelay;
  juce::String equalPitchShift;
  juce::String linkPan;
  juce::String flipPan;
  juce::String bypass;

  juce::String tooltipOffset;
  juce::String tooltipLeftPan;
  juce::String tooltipRightPan;
  juce::String tooltipPitchShift;
  juce::String tooltipHaasAmount;
  juce::String tooltipOutputGain;
  juce::String tooltipEqualDelay;
  juce::String tooltipEqualPitchShift;
  juce::String tooltipLinkPan;
  juce::String tooltipFlipPan;
  juce::String tooltipSettings;
  juce::String tooltipCloseSettings;

  juce::String language;
  juce::String checkForUpdates;
  juce::String currentVersionPrefix;
  juce::String checking;
  juce::String updateAvailablePrefix;
  juce::String upToDate;
  juce::String updateCheckFailed;
  juce::String disclaimerLine1;
  juce::String disclaimerLine2;
  juce::String releasesLink;

  juce::String leftChannel;
  juce::String rightChannel;
  juce::String delay;
  juce::String pitch;
  juce::String reportedLatencyPrefix;
  juce::String haasPrecedencePrefix;
  juce::String statusOff;
  juce::String statusNone;
  juce::String statusLeft;
  juce::String statusRight;
  juce::String statusAmbiguous;
  juce::String leftGain;
  juce::String rightGain;
  juce::String stereoWarning;
};

UILanguage languageFromCode(const juce::String &languageCode) {
  return VocalWidenerProcessor::normaliseLanguageCode(languageCode) == "ja"
             ? UILanguage::japanese
             : UILanguage::english;
}

juce::String languageCodeFor(UILanguage language) {
  return language == UILanguage::japanese ? "ja" : "en";
}

int comboIdForLanguage(UILanguage language) {
  return language == UILanguage::japanese ? 2 : 1;
}

UILanguage languageForComboId(int comboId) {
  return comboId == 2 ? UILanguage::japanese : UILanguage::english;
}

const LocalizedStrings &getStrings(UILanguage language) {
  static const LocalizedStrings english {
      "topaz",
      "pan",
      "unpan",
      "settings",
      true,
      "offset time",
      "left pan",
      "right pan",
      "pitch shift",
      "haas comp",
      "output gain",
      "equal delay",
      "equal pitch shift",
      "link pan",
      "flip pan",
      "bypass",
      "sets the delay between the left and right channels",
      "sets how far left the left voice sits",
      "sets how far right the right voice sits",
      "adds subtle pitch separation between channels",
      "balances perceived loudness when channels are delayed",
      "controls the final output volume",
      "offsets delay equally across both channels",
      "splits pitch shift evenly between negative left and positive right",
      "locks both pan amounts together",
      "swaps the left and right pan destinations",
      "settings",
      "close settings",
      "language",
      "check for updates",
      "current: ",
      "checking...",
      "update available: ",
      "up to date",
      "update check failed",
      "update checks may be incomplete or inaccurate",
      "verify the latest version on the link below:",
      "github releases",
      "left channel",
      "right channel",
      "delay",
      "pitch",
      "reported latency: ",
      "haas precedence: ",
      "off",
      "none",
      "left",
      "right",
      "ambiguous",
      "left gain",
      "right gain",
      "Warning: Plugin requires a Stereo track layout to function properly."};

  static const LocalizedStrings japanese {
      juce::String::fromUTF8("とぱず"),
      juce::String::fromUTF8("パン"),
      juce::String::fromUTF8("パン"),
      juce::String::fromUTF8("設定"),
      false,
      juce::String::fromUTF8("オフセット時間"),
      juce::String::fromUTF8("左パン"),
      juce::String::fromUTF8("右パン"),
      juce::String::fromUTF8("ピッチシフト"),
      juce::String::fromUTF8("ハース補正"),
      juce::String::fromUTF8("出力ゲイン"),
      juce::String::fromUTF8("均等ディレイ"),
      juce::String::fromUTF8("均等ピッチ"),
      juce::String::fromUTF8("パンリンク"),
      juce::String::fromUTF8("パン反転"),
      juce::String::fromUTF8("バイパス"),
      juce::String::fromUTF8("左右チャンネルの時間差を設定します"),
      juce::String::fromUTF8("左側の声をどこまで左に置くかを決めます"),
      juce::String::fromUTF8("右側の声をどこまで右に置くかを決めます"),
      juce::String::fromUTF8("左右チャンネルに微小なピッチ差を加えます"),
      juce::String::fromUTF8("時間差で偏って聞こえる音量感を補正します"),
      juce::String::fromUTF8("最終的な出力音量を調整します"),
      juce::String::fromUTF8("左右の時間差を中央基準で扱います"),
      juce::String::fromUTF8("左右に均等なピッチ差を付けます"),
      juce::String::fromUTF8("左右のパン量を連動させます"),
      juce::String::fromUTF8("左右のパン先を入れ替えます"),
      juce::String::fromUTF8("設定"),
      juce::String::fromUTF8("設定を閉じる"),
      juce::String::fromUTF8("言語"),
      juce::String::fromUTF8("アップデートを確認"),
      juce::String::fromUTF8("現在のバージョン: "),
      juce::String::fromUTF8("確認中..."),
      juce::String::fromUTF8("更新があります: "),
      juce::String::fromUTF8("最新版です"),
      juce::String::fromUTF8("アップデートを確認できませんでした"),
      juce::String::fromUTF8("更新確認の結果が不完全または不正確な場合があります"),
      juce::String::fromUTF8("必ず下のリンクから最新版も確認してください"),
      "GitHub Releases",
      juce::String::fromUTF8("左チャンネル"),
      juce::String::fromUTF8("右チャンネル"),
      juce::String::fromUTF8("ディレイ"),
      juce::String::fromUTF8("ピッチ"),
      juce::String::fromUTF8("プラグインのレイテンシ: "),
      juce::String::fromUTF8("ハース先行: "),
      juce::String::fromUTF8("オフ"),
      juce::String::fromUTF8("なし"),
      juce::String::fromUTF8("左"),
      juce::String::fromUTF8("右"),
      juce::String::fromUTF8("不明"),
      juce::String::fromUTF8("左ゲイン"),
      juce::String::fromUTF8("右ゲイン"),
      juce::String::fromUTF8(
          "警告: このプラグインはステレオトラックでのみ正しく動作します。")};

  return language == UILanguage::japanese ? japanese : english;
}

const LocalizedStrings &getStringsForCode(const juce::String &languageCode) {
  return getStrings(languageFromCode(languageCode));
}

juce::Path createSettingsGearPath() {
  juce::Path path;
  path.setUsingNonZeroWinding(false);

  const auto centre = juce::Point<float>(50.0f, 50.0f);
  const auto spoke = juce::Rectangle<float>(46.0f, 6.0f, 8.0f, 18.0f);

  for (int i = 0; i < 8; ++i) {
    path.addRoundedRectangle(
        spoke.transformedBy(juce::AffineTransform::rotation(
                                juce::MathConstants<float>::twoPi *
                                    (static_cast<float>(i) / 8.0f),
                                centre.x, centre.y)),
        2.0f);
  }

  path.addEllipse(20.0f, 20.0f, 60.0f, 60.0f);
  path.addEllipse(37.0f, 37.0f, 26.0f, 26.0f);
  return path;
}

struct UpdateCheckResult {
  enum class Status { upToDate, updateAvailable, failed };

  Status status = Status::failed;
  juce::String latestTag;
};

UpdateCheckResult fetchLatestRelease() {
  UpdateCheckResult result;
  int statusCode = 0;

  auto options =
      juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
          .withHttpRequestCmd("GET")
          .withExtraHeaders("User-Agent: topaz-pan\r\n"
                            "Accept: application/vnd.github+json\r\n")
          .withConnectionTimeoutMs(5000)
          .withNumRedirectsToFollow(4)
          .withStatusCode(&statusCode);

  auto stream =
      VocalWidenerProcessor::getLatestReleaseApiUrl().createInputStream(options);

  if (stream == nullptr)
    return result;

  if (statusCode >= 400)
    return result;

  const auto responseText = stream->readEntireStreamAsString();
  const auto parsed = juce::JSON::parse(responseText);
  auto *object = parsed.getDynamicObject();

  if (object == nullptr)
    return result;

  const auto latestTag = normaliseVersionTag(object->getProperty("tag_name").toString());

  if (latestTag.isEmpty())
    return result;

  result.latestTag = latestTag;
  result.status = isUpdateAvailable(VocalWidenerProcessor::getVersionTag(),
                                    latestTag)
                      ? UpdateCheckResult::Status::updateAvailable
                      : UpdateCheckResult::Status::upToDate;
  return result;
}

class SettingsTitleComponent : public juce::Component {
public:
  void setText(const juce::String &newText) {
    if (titleText == newText)
      return;

    titleText = newText;
    repaint();
  }

  void paint(juce::Graphics &g) override {
    const auto bounds = getLocalBounds().toFloat();
    auto font = makeHelveticaFont(32.0f, juce::Font::bold);
    const float baselineY =
        bounds.getCentreY() - (font.getHeight() * 0.5f) + font.getAscent();

    g.setColour(juce::Colours::white);
    g.setFont(font);

    float cursorX = bounds.getX() + titleAreaHorizontalPadding;
    for (int i = 0; i < titleText.length(); ++i) {
      const juce::String glyph = juce::String::charToString(titleText[i]);
      const float glyphWidth = measureTextWidth(font, glyph);
      g.drawText(glyph,
                 juce::Rectangle<float>(cursorX, baselineY - font.getAscent(),
                                        glyphWidth + 4.0f, font.getHeight()),
                 juce::Justification::centredLeft, false);
      cursorX += glyphWidth;
    }
  }

private:
  juce::String titleText {"settings"};
};

class SettingsOverlay : public juce::Component {
public:
  SettingsOverlay(CustomLookAndFeel &lookAndFeelRef,
                  const juce::String &initialLanguageCode,
                  std::function<void(const juce::String &)> onLanguageChangedIn,
                  std::function<void()> onCloseIn)
      : lookAndFeel(lookAndFeelRef),
        onLanguageChanged(std::move(onLanguageChangedIn)),
        onClose(std::move(onCloseIn)) {
    setLookAndFeel(&lookAndFeel);
    setInterceptsMouseClicks(true, true);
    setOpaque(true);

	    addAndMakeVisible(titleGraphic);

    addAndMakeVisible(closeButton);
    closeButton.setButtonText("X");
    closeButton.onClick = [this] {
      if (onClose)
        onClose();
    };
	    closeButton.getProperties().set("settingsClose", true);
	    closeButton.setColour(juce::TextButton::buttonColourId,
	                          juce::Colours::transparentBlack);
    closeButton.setColour(juce::TextButton::buttonOnColourId,
                          juce::Colours::transparentBlack);
    closeButton.setColour(juce::TextButton::textColourOffId,
                          juce::Colours::white);
    closeButton.setTooltip(getStringsForCode(initialLanguageCode).tooltipCloseSettings);

    addAndMakeVisible(languageLabel);
    languageLabel.setColour(juce::Label::textColourId,
                            juce::Colours::white);
    languageLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(languageButton);
    languageButton.getProperties().set("settingsCombo", true);
    languageButton.onClick = [this] { showLanguageMenu(); };
    languageButton.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::transparentBlack);
    languageButton.setColour(juce::TextButton::buttonOnColourId,
                             juce::Colours::transparentBlack);
    languageButton.setColour(juce::TextButton::textColourOffId,
                             juce::Colours::white);

	    addAndMakeVisible(checkForUpdatesButton);
	    checkForUpdatesButton.onClick = [this] { beginUpdateCheck(); };
	    checkForUpdatesButton.getProperties().set("settingsAction", true);
	    checkForUpdatesButton.setColour(juce::TextButton::buttonColourId,
	                                    juce::Colours::white.withAlpha(0.035f));
	    checkForUpdatesButton.setColour(juce::TextButton::buttonOnColourId,
	                                    juce::Colours::white.withAlpha(0.08f));
	    checkForUpdatesButton.setColour(juce::TextButton::textColourOffId,
	                                    juce::Colours::white);

	    addAndMakeVisible(updateStatusLabel);
	    updateStatusLabel.setJustificationType(juce::Justification::centredLeft);
	    updateStatusLabel.setColour(juce::Label::textColourId,
	                                juce::Colours::white.withAlpha(0.62f));

    addAndMakeVisible(disclaimerLabel);
	    disclaimerLabel.setColour(juce::Label::textColourId,
	                              juce::Colours::white.withAlpha(0.55f));
    disclaimerLabel.setJustificationType(juce::Justification::centredLeft);
    disclaimerLabel.setFont(makeHelveticaFont(11.0f));

    addAndMakeVisible(disclaimerDetailLabel);
	    disclaimerDetailLabel.setColour(juce::Label::textColourId,
	                                    juce::Colours::white.withAlpha(0.55f));
    disclaimerDetailLabel.setJustificationType(juce::Justification::centredLeft);
    disclaimerDetailLabel.setFont(makeHelveticaFont(11.0f));

    addAndMakeVisible(releasesLinkButton);
    releasesLinkButton.getProperties().set("settingsLink", true);
    releasesLinkButton.onClick = [] {
      VocalWidenerProcessor::getReleasesPageUrl().launchInDefaultBrowser();
    };
    releasesLinkButton.setColour(juce::TextButton::textColourOffId,
                                 juce::Colours::white.withAlpha(0.78f));

    setLanguageCode(initialLanguageCode);
  }

  ~SettingsOverlay() override { setLookAndFeel(nullptr); }

  void paint(juce::Graphics &g) override {
	    g.fillAll(juce::Colour::fromString("#FF7BBED4"));

	    g.setColour(juce::Colours::white.withAlpha(0.10f));
	    const float footerRuleY = static_cast<float>(getHeight() - 146);
	    g.drawLine(30.0f, footerRuleY, static_cast<float>(getWidth() - 30),
	               footerRuleY, 1.0f);
  }

  void resized() override {
	    const int leftMargin = 30;
	    const int labelW = 100;
	    const int sliderW = 210;
	    const int rowH = 34;
	    const int rightEdge = getWidth() - 30;

	    titleGraphic.setBounds(leftMargin, 20, getWidth() - 60, titleAreaHeight);
    closeButton.setBounds(getWidth() - 58, 27, 32, 32);

	    int yStart = 108;
	    languageLabel.setBounds(leftMargin, yStart, labelW, rowH);
	    languageButton.setBounds(leftMargin + labelW, yStart, sliderW + 52, rowH);
	    yStart += rowH + 24;

	    checkForUpdatesButton.setBounds(leftMargin + labelW, yStart, sliderW + 52,
	                                    rowH);
	    yStart += rowH + 16;

	    updateStatusLabel.setBounds(leftMargin + labelW, yStart, sliderW + 52, 18);

	    const int footerY = getHeight() - 118;
	    disclaimerLabel.setBounds(leftMargin, footerY, rightEdge - leftMargin, 16);
	    disclaimerDetailLabel.setBounds(leftMargin, footerY + 18, rightEdge - leftMargin, 16);
    releasesLinkButton.setBounds(leftMargin, footerY + 40, 140, 20);
  }

  void mouseUp(const juce::MouseEvent &event) override {
	    if (!getPanelBounds().contains(event.getPosition())) {
      if (onClose)
        onClose();
    }
  }

  void setLanguageCode(const juce::String &newLanguageCode) {
    languageCode = VocalWidenerProcessor::normaliseLanguageCode(newLanguageCode);
    applyLocalisation();
  }

private:
  juce::Rectangle<int> getPanelBounds() const {
	    return getLocalBounds();
  }

  void refreshUpdateStatusText() {
    const auto &strings = getStringsForCode(languageCode);

    switch (updateStatus) {
    case UpdateStatus::currentVersion:
      updateStatusLabel.setText(
          strings.currentVersionPrefix + VocalWidenerProcessor::getVersionTag(),
          juce::dontSendNotification);
      break;
    case UpdateStatus::checking:
      updateStatusLabel.setText(strings.checking, juce::dontSendNotification);
      break;
    case UpdateStatus::updateAvailable:
      updateStatusLabel.setText(strings.updateAvailablePrefix + latestTag,
                                juce::dontSendNotification);
      break;
    case UpdateStatus::upToDate:
      updateStatusLabel.setText(strings.upToDate, juce::dontSendNotification);
      break;
    case UpdateStatus::failed:
      updateStatusLabel.setText(strings.updateCheckFailed,
                                juce::dontSendNotification);
      break;
    }
  }

  void showLanguageMenu() {
    juce::PopupMenu menu;
    const auto currentLanguage = languageFromCode(languageCode);
    menu.setLookAndFeel(&lookAndFeel);
    menu.addItem(comboIdForLanguage(UILanguage::english), "English", true,
                 currentLanguage == UILanguage::english);
    menu.addItem(comboIdForLanguage(UILanguage::japanese),
                 juce::String::fromUTF8("日本語"), true,
                 currentLanguage == UILanguage::japanese);

    auto options = juce::PopupMenu::Options()
                       .withTargetComponent(languageButton)
                       .withMinimumWidth(languageButton.getWidth())
                       .withMaximumNumColumns(1)
                       .withInitiallySelectedItem(
                           comboIdForLanguage(currentLanguage))
                       .withItemThatMustBeVisible(
                           comboIdForLanguage(currentLanguage))
                       .withStandardItemHeight(languageButton.getHeight());

    juce::Component::SafePointer<SettingsOverlay> safeThis(this);
    menu.showMenuAsync(options, [safeThis](int result) {
      if (safeThis == nullptr || result == 0)
        return;

      const auto selectedLanguage = languageForComboId(result);
      safeThis->languageCode = languageCodeFor(selectedLanguage);
      safeThis->applyLocalisation();

      if (safeThis->onLanguageChanged)
        safeThis->onLanguageChanged(safeThis->languageCode);
    });
  }

  void applyLocalisation() {
    const auto &strings = getStringsForCode(languageCode);
    titleGraphic.setText(strings.settingsTitle);
    closeButton.setTooltip(strings.tooltipCloseSettings);
    languageLabel.setText(strings.language, juce::dontSendNotification);
    languageButton.setButtonText(
        languageFromCode(languageCode) == UILanguage::japanese
            ? juce::String::fromUTF8("日本語")
            : "English");
    checkForUpdatesButton.setButtonText(strings.checkForUpdates);
    disclaimerLabel.setText(strings.disclaimerLine1, juce::dontSendNotification);
    disclaimerDetailLabel.setText(strings.disclaimerLine2,
                                  juce::dontSendNotification);
    releasesLinkButton.setButtonText(strings.releasesLink);
    refreshUpdateStatusText();
    resized();
    repaint();
  }

  void beginUpdateCheck() {
    if (isCheckingForUpdates)
      return;

    isCheckingForUpdates = true;
    checkForUpdatesButton.setEnabled(false);
    updateStatus = UpdateStatus::checking;
    refreshUpdateStatusText();

    juce::Component::SafePointer<SettingsOverlay> safeThis(this);
    std::thread([safeThis] {
      const auto result = fetchLatestRelease();

      juce::MessageManager::callAsync([safeThis, result] {
        if (safeThis == nullptr)
          return;

        safeThis->isCheckingForUpdates = false;
        safeThis->checkForUpdatesButton.setEnabled(true);

        switch (result.status) {
        case UpdateCheckResult::Status::updateAvailable:
          safeThis->updateStatus = UpdateStatus::updateAvailable;
          safeThis->latestTag = result.latestTag;
          break;
        case UpdateCheckResult::Status::upToDate:
          safeThis->updateStatus = UpdateStatus::upToDate;
          safeThis->latestTag.clear();
          break;
        case UpdateCheckResult::Status::failed:
          safeThis->updateStatus = UpdateStatus::failed;
          safeThis->latestTag.clear();
          break;
        }

        safeThis->refreshUpdateStatusText();
        safeThis->resized();
        safeThis->repaint();
      });
    }).detach();
  }

  enum class UpdateStatus {
    currentVersion,
    checking,
    updateAvailable,
    upToDate,
    failed
  };

  CustomLookAndFeel &lookAndFeel;
  std::function<void(const juce::String &)> onLanguageChanged;
  std::function<void()> onClose;
  SettingsTitleComponent titleGraphic;
  juce::TextButton closeButton;
  juce::Label languageLabel;
  juce::TextButton languageButton;
  juce::TextButton checkForUpdatesButton;
  juce::Label updateStatusLabel;
  juce::Label disclaimerLabel;
  juce::Label disclaimerDetailLabel;
  juce::TextButton releasesLinkButton;
  bool isCheckingForUpdates = false;
  juce::String languageCode {"en"};
  UpdateStatus updateStatus {UpdateStatus::currentVersion};
  juce::String latestTag;
};
} // namespace

class TitleComponent : public juce::Component {
public:
  explicit TitleComponent(VocalWidenerProcessor &processorRef,
                          std::function<juce::String()> getLanguageCodeIn)
      : processor(processorRef),
        getLanguageCode(std::move(getLanguageCodeIn)) {
    setInterceptsMouseClicks(false, false);
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    g.reduceClipRegion(bounds.getSmallestIntegerContainer());

    const auto titleState = computeTitleState(bounds);
    const auto &strings = getStringsForCode(getLanguageCode());
    auto font = makeHelveticaFont(32.0f * titleState.scale, juce::Font::bold);
    const juce::String topazWord =
        titleState.allCaps ? strings.titleLeadingWord.toUpperCase()
                           : strings.titleLeadingWord;
    const juce::String panWord =
        titleState.allCaps ? titleState.trailingWord.toUpperCase()
                           : titleState.trailingWord;

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
    juce::String trailingWord = "pan";
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
    const bool bypassed =
        processor.bypassParam->load(std::memory_order_relaxed) > 0.5f;
    const bool flipPan =
        processor.flipPanParam->load(std::memory_order_relaxed) > 0.5f;

    state.scale = mapOutputScale(outputGain);
    const auto &strings = getStringsForCode(getLanguageCode());
    state.allCaps =
        strings.allowTitleAllCaps &&
        outputGain.value >=
            (outputGain.max - juce::jmax(0.0001f,
                                         (outputGain.max - outputGain.min) *
                                             0.001f));
    state.trailingWord = bypassed ? strings.titleBypassTrailingWord
                                  : strings.titleTrailingWord;
    state.letterTracking =
        mapLetterTracking(leftPanAmount.value, rightPanAmount.value, flipPan,
                          state.scale);

    const juce::String topazWord =
        state.allCaps ? strings.titleLeadingWord.toUpperCase()
                      : strings.titleLeadingWord;
    const juce::String panWord =
        state.allCaps ? state.trailingWord.toUpperCase() : state.trailingWord;
    auto font = makeHelveticaFont(32.0f * state.scale, juce::Font::bold);
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

    if (bypassed) {
      state.chromaOffset *= 0.2f;
      state.chromaAlpha *= 0.18f;
    }

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
        juce::jmax(8.0f, measureTextWidth(font, " ") * 0.95f);
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
      const float glyphWidth = measureTextWidth(font, glyph);
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
      const float glyphWidth = measureTextWidth(font, glyph);
      g.drawText(glyph,
                 juce::Rectangle<float>(cursorX, baselineY - font.getAscent(),
                                        glyphWidth + 4.0f, font.getHeight()),
                 juce::Justification::centredLeft, false);

      if (i < word.length() - 1)
        cursorX += computeAdvance(glyphWidth, tracking);
    }
  }

  VocalWidenerProcessor &processor;
  std::function<juce::String()> getLanguageCode;
};

//==============================================================================
CustomLookAndFeel::CustomLookAndFeel() {
  setDefaultSansSerifTypefaceName("Helvetica Neue");
  setColour(juce::PopupMenu::backgroundColourId,
            juce::Colour::fromString("#FF86C3D7"));
  setColour(juce::PopupMenu::textColourId, juce::Colours::white);
  setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
  setColour(juce::PopupMenu::highlightedBackgroundColourId,
            juce::Colours::white.withAlpha(0.14f));
}

void CustomLookAndFeel::setLanguageCode(const juce::String &newLanguageCode) {
  languageCode = VocalWidenerProcessor::normaliseLanguageCode(newLanguageCode);
}

juce::Font CustomLookAndFeel::getLabelFont(juce::Label &) {
  return isJapaneseLanguageCode(languageCode) ? makeMultilingualSansFont(13.0f)
                                              : makeHelveticaFont(13.3f);
}

juce::Font CustomLookAndFeel::getTextButtonFont(juce::TextButton &button,
                                                int buttonHeight) {
  const bool japanese = isJapaneseLanguageCode(languageCode);

  if (button.getProperties().getWithDefault("settingsClose", false))
    return japanese ? makeMultilingualSansFont(static_cast<float>(buttonHeight) *
                                                   0.64f,
                                               juce::Font::bold)
                    : makeHelveticaFont(static_cast<float>(buttonHeight) * 0.7f,
                                        juce::Font::bold);

  if (button.getProperties().getWithDefault("settingsLink", false))
    return japanese ? makeMultilingualSansFont(10.8f)
                    : makeHelveticaFont(11.5f);

  return japanese
             ? makeMultilingualSansFont(static_cast<float>(buttonHeight) * 0.46f,
                                        juce::Font::bold)
             : makeHelveticaFont(static_cast<float>(buttonHeight) * 0.5f,
                                 juce::Font::bold);
}

void CustomLookAndFeel::drawButtonBackground(
    juce::Graphics &g, juce::Button &button, const juce::Colour &backgroundColour,
    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
  if (button.getProperties().getWithDefault("settingsClose", false))
  {
    auto bounds = button.getLocalBounds().toFloat().reduced(3.0f);
    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown) {
      g.setColour(juce::Colours::white.withAlpha(shouldDrawButtonAsDown ? 0.10f
                                                                        : 0.05f));
      g.fillRect(bounds);
    }

    g.setColour(juce::Colours::white.withAlpha(0.14f));
    g.drawRect(bounds, 1.0f);

    const auto iconBounds = bounds.reduced(bounds.getWidth() * 0.28f,
                                           bounds.getHeight() * 0.28f);
    g.setColour(button.findColour(juce::TextButton::textColourOffId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
    g.drawLine(juce::Line<float>(iconBounds.getTopLeft(),
                                 iconBounds.getBottomRight()),
               1.8f);
    g.drawLine(juce::Line<float>(iconBounds.getBottomLeft(),
                                 iconBounds.getTopRight()),
               1.8f);
    return;
  }

  if (button.getProperties().getWithDefault("settingsLink", false))
    return;

  auto bounds = button.getLocalBounds().toFloat();
  auto fill = backgroundColour;
  const bool isSettingsAction =
      button.getProperties().getWithDefault("settingsAction", false);
  const bool isSettingsCombo =
      button.getProperties().getWithDefault("settingsCombo", false);

  if (shouldDrawButtonAsDown)
    fill = fill.brighter(0.16f);
  else if (shouldDrawButtonAsHighlighted)
    fill = fill.brighter(0.08f);

  if (isSettingsAction)
    fill = juce::Colours::white.withAlpha(shouldDrawButtonAsDown ? 0.06f
                                                                 : (shouldDrawButtonAsHighlighted ? 0.03f
                                                                                                  : 0.0f));

  if (fill.getAlpha() > 0)
  {
    g.setColour(fill);
    g.fillRect(bounds);
  }

  g.setColour(juce::Colours::white.withAlpha(
      (isSettingsAction || isSettingsCombo) ? 0.22f : 0.18f));
  g.drawRect(bounds, 1.0f);

  if (isSettingsCombo) {
    juce::Path arrow;
    const float centreX = bounds.getRight() - 18.0f;
    const float centreY = bounds.getCentreY();
    arrow.startNewSubPath(centreX - 6.0f, centreY - 3.0f);
    arrow.lineTo(centreX, centreY + 3.0f);
    arrow.lineTo(centreX + 6.0f, centreY - 3.0f);

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.strokePath(arrow, juce::PathStrokeType(2.2f));
  }
}

void CustomLookAndFeel::drawButtonText(juce::Graphics &g,
                                       juce::TextButton &button,
                                       bool shouldDrawButtonAsHighlighted,
                                       bool shouldDrawButtonAsDown) {
  juce::ignoreUnused(shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
  auto colour =
      button.findColour(juce::TextButton::textColourOffId).withMultipliedAlpha(
          button.isEnabled() ? 1.0f : 0.45f);
  g.setColour(colour);
  g.setFont(getTextButtonFont(button, button.getHeight()));
  const bool isClose = button.getProperties().getWithDefault("settingsClose", false);
  const bool isSettingsAction =
      button.getProperties().getWithDefault("settingsAction", false);
  const bool isSettingsLink =
      button.getProperties().getWithDefault("settingsLink", false);
  const bool isSettingsCombo =
      button.getProperties().getWithDefault("settingsCombo", false);

  if (isClose)
    return;

  const auto justification =
      isSettingsAction ? juce::Justification::centred
                       : juce::Justification::centredLeft;
  g.drawText(button.getButtonText(),
             button.getLocalBounds().toFloat().reduced(
                 (isSettingsAction || isSettingsLink) ? 0.0f
                                                      : (isSettingsCombo ? 12.0f
                                                                         : 12.0f),
                 0.0f),
             justification,
             false);
}

juce::Font CustomLookAndFeel::getPopupMenuFont() {
  return makeMultilingualSansFont(isJapaneseLanguageCode(languageCode) ? 14.0f
                                                                       : 15.0f);
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
  g.setFont(isJapaneseLanguageCode(languageCode) ? makeMultilingualSansFont(13.0f)
                                                 : makeHelveticaFont(14.0f));
  g.drawText(text, 0, 0, width, height, juce::Justification::centred, true);
}

void CustomLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox &,
                                                 juce::Graphics &g,
                                                 const juce::Path &path,
                                                 juce::Image &) {
  g.setColour(juce::Colour::fromString("#F1E8F7FA"));
  g.fillPath(path);

  g.setColour(juce::Colour::fromString("#334A8A99"));
  g.strokePath(path, juce::PathStrokeType(1.0f));
}

int CustomLookAndFeel::getCallOutBoxBorderSize(const juce::CallOutBox &) {
  return 18;
}

float CustomLookAndFeel::getCallOutBoxCornerSize(const juce::CallOutBox &) {
  return 0.0f;
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
  g.setFont(isJapaneseLanguageCode(languageCode) ? makeMultilingualSansFont(13.0f)
                                                 : makeHelveticaFont(13.2f));
  g.drawText(button.getButtonText(),
             juce::Rectangle<float>(switchRect.getRight() + 8.0f, 0.0f,
                                    bounds.getWidth() - switchW - 8.0f,
                                    bounds.getHeight()),
             juce::Justification::centredLeft, true);
}

//==============================================================================
VocalWidenerEditor::VocalWidenerEditor(VocalWidenerProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setLookAndFeel(&customLookAndFeel);
  currentLanguageCode = audioProcessor.getLanguageCode();
  customLookAndFeel.setLanguageCode(currentLanguageCode);
  titleComponent = std::make_unique<TitleComponent>(
      audioProcessor, [this] { return currentLanguageCode; });
  settingsOverlay = std::make_unique<SettingsOverlay>(
      customLookAndFeel, currentLanguageCode,
      [this](const juce::String &selectedLanguageCode) {
        setCurrentLanguageCode(selectedLanguageCode);
      },
      [this] { setSettingsVisible(false); });
  addAndMakeVisible(*titleComponent);
  addChildComponent(*settingsOverlay);
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

  addAndMakeVisible(equalPitchToggle);
  equalPitchToggle.setTooltip(
      "splits pitch shift evenly between negative left and positive right");
  attEqualPitch =
      std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
          audioProcessor.apvts, "equalPitchShift", equalPitchToggle);

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
  addAndMakeVisible(latencyLabel);
  addAndMakeVisible(versionLabel);
  addAndMakeVisible(settingsButton);

  leftReadout.setJustificationType(juce::Justification::centredLeft);
  rightReadout.setJustificationType(juce::Justification::centredLeft);
  haasReadout.setJustificationType(juce::Justification::centredLeft);

  leftReadout.setColour(juce::Label::textColourId, juce::Colours::white);
  rightReadout.setColour(juce::Label::textColourId, juce::Colours::white);
  haasReadout.setColour(juce::Label::textColourId, juce::Colours::white);
  leftReadout.setMinimumHorizontalScale(1.0f);
  rightReadout.setMinimumHorizontalScale(1.0f);
  haasReadout.setMinimumHorizontalScale(1.0f);
  latencyLabel.setColour(juce::Label::textColourId,
                         juce::Colours::white.withAlpha(0.55f));
  latencyLabel.setJustificationType(juce::Justification::centredLeft);
  latencyLabel.setInterceptsMouseClicks(false, false);
  versionLabel.setColour(juce::Label::textColourId,
                         juce::Colours::white.withAlpha(0.55f));
  versionLabel.setJustificationType(juce::Justification::centredRight);
  versionLabel.setText(VocalWidenerProcessor::getVersionTag(),
                       juce::dontSendNotification);
  versionLabel.setInterceptsMouseClicks(false, false);

  settingsButton.setShape(createSettingsGearPath(), false, true, false);
  settingsButton.setBorderSize(juce::BorderSize<int>(2));
  settingsButton.onClick = [this] { showSettingsPopup(); };

  applyLocalisation();
  updateHaasCompVisualState(
      audioProcessor.haasCompEnableParam->load(std::memory_order_relaxed) > 0.5f &&
      audioProcessor.linkPanParam->load(std::memory_order_relaxed) > 0.5f);
  updatePanUnitLabels(
      audioProcessor.flipPanParam->load(std::memory_order_relaxed) > 0.5f);

  startTimerHz(30);
}

VocalWidenerEditor::~VocalWidenerEditor() { setLookAndFeel(nullptr); }

void VocalWidenerEditor::setCurrentLanguageCode(
    const juce::String &newLanguageCode) {
  const auto normalised =
      VocalWidenerProcessor::normaliseLanguageCode(newLanguageCode);

  if (currentLanguageCode == normalised)
    return;

  currentLanguageCode = normalised;
  audioProcessor.setLanguageCode(currentLanguageCode);
  applyLocalisation();
}

void VocalWidenerEditor::applyLocalisation() {
  const auto &strings = getStringsForCode(currentLanguageCode);
  customLookAndFeel.setLanguageCode(currentLanguageCode);
  const auto readoutFont = makeHelveticaFont(
      readoutFontHeightForLanguage(currentLanguageCode));

  offsetLabel.setText(strings.offsetTime, juce::dontSendNotification);
  leftPanLabel.setText(strings.leftPan, juce::dontSendNotification);
  rightPanLabel.setText(strings.rightPan, juce::dontSendNotification);
  pitchDiffLabel.setText(strings.pitchShift, juce::dontSendNotification);
  haasCompAmtLabel.setText(strings.haasComp, juce::dontSendNotification);
  outGainLabel.setText(strings.outputGain, juce::dontSendNotification);

  centeredToggle.setButtonText(strings.equalDelay);
  equalPitchToggle.setButtonText(strings.equalPitchShift);
  linkPanToggle.setButtonText(strings.linkPan);
  flipPanToggle.setButtonText(strings.flipPan);
  haasCompToggle.setButtonText(strings.haasComp);
  bypassToggle.setButtonText(strings.bypass);

  offsetLabel.setTooltip(strings.tooltipOffset);
  leftPanLabel.setTooltip(strings.tooltipLeftPan);
  rightPanLabel.setTooltip(strings.tooltipRightPan);
  pitchDiffLabel.setTooltip(strings.tooltipPitchShift);
  haasCompAmtLabel.setTooltip(strings.tooltipHaasAmount);
  outGainLabel.setTooltip(strings.tooltipOutputGain);
  centeredToggle.setTooltip(strings.tooltipEqualDelay);
  equalPitchToggle.setTooltip(strings.tooltipEqualPitchShift);
  linkPanToggle.setTooltip(strings.tooltipLinkPan);
  flipPanToggle.setTooltip(strings.tooltipFlipPan);
  settingsButton.setTooltip(strings.tooltipSettings);

  latencyLabel.setText(strings.reportedLatencyPrefix + "0.00 ms",
                       juce::dontSendNotification);

  if (settingsOverlay != nullptr)
    if (auto *overlay = dynamic_cast<SettingsOverlay *>(settingsOverlay.get()))
      overlay->setLanguageCode(currentLanguageCode);

  if (titleComponent != nullptr)
    titleComponent->repaint();

  leftReadout.setFont(readoutFont);
  rightReadout.setFont(readoutFont);
  haasReadout.setFont(readoutFont);
}

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

void VocalWidenerEditor::showSettingsPopup() {
  setSettingsVisible(settingsOverlay != nullptr && !settingsOverlay->isVisible());
}

void VocalWidenerEditor::setSettingsVisible(bool visible) {
  if (settingsOverlay == nullptr)
    return;

  settingsOverlay->setVisible(visible);

  if (visible)
    settingsOverlay->toFront(false);
}

void VocalWidenerEditor::timerCallback() {
  if (!audioProcessor.isStereoLayout)
    return; // Banner will cover UI

  const auto &strings = getStringsForCode(currentLanguageCode);

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
  repaint();

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
      strings.leftChannel + ":\n" + strings.delay + ": " +
          juce::String::formatted("%+.2f", oDel) + " ms\n" + strings.pitch +
          ": " + juce::String::formatted("%+.2f", oPit) + " c",
      juce::dontSendNotification);
  rightReadout.setText(
      strings.rightChannel + ":\n" + strings.delay + ": " +
          juce::String::formatted("%+.2f", dDel) + " ms\n" + strings.pitch +
          ": " + juce::String::formatted("%+.2f", dPit) + " c",
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

  juce::String precStr = strings.haasPrecedencePrefix;
  if (!effectiveHaasEnabled)
    precStr += strings.statusOff;
  else if (!hasDirectionalPrecedence && !hasAudibleHaasGain)
    precStr += strings.statusNone;
  else if (earlierPath == 0.0f)
    precStr += strings.statusLeft;
  else if (earlierPath == 1.0f)
    precStr += strings.statusRight;
  else
    precStr += strings.statusAmbiguous;

  auto formatCompDb = [](float v) {
    return juce::String(std::abs(v) < 0.005f ? 0.0f : v, 2);
  };
  precStr += "\n" + strings.leftGain + ": " + formatCompDb(oComp) +
             " dB   |   " + strings.rightGain + ": " + formatCompDb(dComp) +
             " dB";
  haasReadout.setText(precStr, juce::dontSendNotification);

  latencyLabel.setText(
      strings.reportedLatencyPrefix +
          juce::String(audioProcessor.getReportedLatencyMs(), 2) + " ms",
      juce::dontSendNotification);
}

void VocalWidenerEditor::paint(juce::Graphics &g) {
  const bool bypassed =
      audioProcessor.bypassParam->load(std::memory_order_relaxed) > 0.5f;
  g.fillAll(bypassed ? juce::Colour::fromString("#FF88AFBD")
                     : juce::Colour::fromString("#FF7BBED4"));

  if (!audioProcessor.isBusesLayoutSupported(audioProcessor.getBusesLayout())) {
    const auto &strings = getStringsForCode(currentLanguageCode);
    g.setColour(juce::Colours::red.withAlpha(0.8f));
    g.fillRect(0, 0, getWidth(), 30);
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(strings.stereoWarning, 0, 0, getWidth(), 30,
               juce::Justification::centred, true);
  }

}

void VocalWidenerEditor::paintOverChildren(juce::Graphics &g) {
  const bool bypassed =
      audioProcessor.bypassParam->load(std::memory_order_relaxed) > 0.5f;

  if (bypassed) {
    g.setColour(juce::Colour::fromFloatRGBA(0.78f, 0.79f, 0.80f, 0.22f));
    g.fillRect(getLocalBounds());
  }
}

void VocalWidenerEditor::resized() {
  if (titleComponent != nullptr)
    titleComponent->setBounds(30, 20, getWidth() - 60, titleAreaHeight);

  if (settingsOverlay != nullptr)
    settingsOverlay->setBounds(getLocalBounds());

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
  equalPitchToggle.setBounds(leftMargin + colWidth + 20, yStart, colWidth, colH);
  yStart += colH;

  linkPanToggle.setBounds(leftMargin, yStart, colWidth, colH);
  flipPanToggle.setBounds(leftMargin + colWidth + 20, yStart, colWidth, colH);
  yStart += colH;

  haasCompToggle.setBounds(leftMargin, yStart, colWidth, colH);
  bypassToggle.setBounds(leftMargin + colWidth + 20, yStart, colWidth, colH);
  yStart += colH + 20;

  leftReadout.setBounds(leftMargin, yStart, 180, 50);
  rightReadout.setBounds(leftMargin + 180, yStart, 180, 50);
  yStart += 55;

  haasReadout.setBounds(leftMargin, yStart, 360, 40);

  const int footerY = getHeight() - 34;
  latencyLabel.setBounds(leftMargin, footerY - 1, 180, 20);
  settingsButton.setBounds(getWidth() - 34, footerY, 18, 18);
  versionLabel.setBounds(getWidth() - 142, footerY - 1, 100, 20);

  if (isJapaneseLanguageCode(currentLanguageCode)) {
    leftReadout.setBounds(leftMargin, yStart - 1, 180, 56);
    rightReadout.setBounds(leftMargin + 180, yStart - 1, 180, 56);
    haasReadout.setBounds(leftMargin, yStart + 54, 360, 48);
  }
}
