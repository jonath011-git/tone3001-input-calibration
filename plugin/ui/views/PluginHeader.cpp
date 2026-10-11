#include "PluginHeader.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/Brand.h"
#include "core/CustomIcons.h"
#include "core/Design.h"
#include "core/Help.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/Clickable.h"
#include "services/Vst3Library.h"

namespace t3k::ui {

namespace {

// A small self-contained calibration panel. It samples the processor's raw
// pre-gain input meter for three seconds, then adjusts the existing Input
// Gain parameter so the level presented to the first chain block approaches
// the requested RMS target. Peak headroom is kept below -1 dBFS.
class InputCalibrationPopup final : public juce::Component, private juce::Timer {
public:
  explicit InputCalibrationPopup(Services& services) : services_(services) {
    setSize(390, 285);
    title_.setText("Input Calibration", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    targetLabel_.setText("Target level at first block (dBFS RMS)", juce::dontSendNotification);
    targetLabel_.setFont(juce::Font(14.0f));
    targetDb_.setText("-20.0");
    targetDb_.setInputRestrictions(12, "0123456789.-");
    targetDb_.setJustification(juce::Justification::centredLeft);
    status_.setText("Play your guitar, then press Measure.", juce::dontSendNotification);
    status_.setFont(juce::Font(14.0f));
    status_.setColour(juce::Label::textColourId, juce::Colour(0xffb8b8bf));
    measured_.setText("Input RMS: waiting for signal", juce::dontSendNotification);
    measured_.setFont(juce::Font(14.0f));
    addAndMakeVisible(title_);
    addAndMakeVisible(targetLabel_);
    addAndMakeVisible(targetDb_);
    addAndMakeVisible(status_);
    addAndMakeVisible(measured_);
    addAndMakeVisible(measureButton_);
    addAndMakeVisible(applyButton_);
    addAndMakeVisible(closeButton_);
    measureButton_.setButtonText("Measure (3 s)");
    applyButton_.setButtonText("Apply gain");
    closeButton_.setButtonText("Close");
    applyButton_.setEnabled(false);
    measureButton_.onClick = [this] { beginMeasurement(); };
    applyButton_.onClick = [this] { applyCalibration(); };
    closeButton_.onClick = [this] {
      if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
    };
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colour(0xff171719));
    g.setColour(juce::Colour(0xff45454c));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 10.0f, 1.0f);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(18);
    title_.setBounds(r.removeFromTop(30));
    r.removeFromTop(12);
    targetLabel_.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    targetDb_.setBounds(r.removeFromTop(32));
    r.removeFromTop(12);
    status_.setBounds(r.removeFromTop(40));
    measured_.setBounds(r.removeFromTop(24));
    r.removeFromTop(8);
    auto buttons = r.removeFromTop(32);
    measureButton_.setBounds(buttons.removeFromLeft(132));
    buttons.removeFromLeft(8);
    applyButton_.setBounds(buttons.removeFromLeft(105));
    buttons.removeFromLeft(8);
    closeButton_.setBounds(buttons);
  }

private:
  void beginMeasurement() {
    if (targetDb_.getText().trim().isEmpty()) {
      status_.setText("Enter a target level first.", juce::dontSendNotification);
      return;
    }
    targetLevelDb_ = juce::jlimit(-36.0f, -3.0f, targetDb_.getText().getFloatValue());
    sumPower_ = 0.0;
    samples_ = 0;
    peakDb_ = -120.0f;
    ticks_ = 0;
    measuredRmsDb_ = -120.0f;
    applyButton_.setEnabled(false);
    measureButton_.setEnabled(false);
    status_.setText("Measuring — play at your normal strength...", juce::dontSendNotification);
    startTimerHz(10);
  }

  void timerCallback() override {
    ++ticks_;
    const float rms = services_.backend.getPreChainInputRmsDb();
    const float peak = services_.backend.getPreChainInputPeakDb();
    if (rms > -60.0f && rms <= 6.0f) {
      sumPower_ += std::pow(10.0, static_cast<double>(rms) / 10.0);
      ++samples_;
      peakDb_ = std::max(peakDb_, peak);
    }
    if (ticks_ < 30) return;
    stopTimer();
    measureButton_.setEnabled(true);
    if (samples_ < 5) {
      status_.setText("No usable signal detected. Check your input and try again.",
                      juce::dontSendNotification);
      measured_.setText("Input RMS: no signal", juce::dontSendNotification);
      return;
    }
    measuredRmsDb_ = static_cast<float>(10.0 * std::log10(sumPower_ / samples_));
    measured_.setText("Measured RMS: " + juce::String(measuredRmsDb_, 1) +
                      " dBFS   |   Peak: " + juce::String(peakDb_, 1) + " dBFS",
                      juce::dontSendNotification);
    status_.setText("Measurement complete. Apply to update the existing Input knob.",
                    juce::dontSendNotification);
    applyButton_.setEnabled(true);
  }

  void applyCalibration() {
    auto* parameter = services_.backend.parameter("inputLevel");
    if (parameter == nullptr || samples_ == 0) {
      status_.setText("Input Gain parameter is unavailable.", juce::dontSendNotification);
      return;
    }
    const float currentNormalized = parameter->getValue();
    const float currentGainDb = (currentNormalized - 0.5f) * 48.0f;
    const float measuredAtChainDb = measuredRmsDb_ + currentGainDb;
    float correctionDb = targetLevelDb_ - measuredAtChainDb;

    // Avoid pushing measured peaks above -1 dBFS after the current Input gain.
    const float peakAtChainDb = peakDb_ + currentGainDb;
    const float safeCorrectionDb = -1.0f - peakAtChainDb;
    correctionDb = std::min(correctionDb, safeCorrectionDb);
    const float newGainDb = juce::jlimit(-24.0f, 24.0f, currentGainDb + correctionDb);
    const float newNormalized = juce::jlimit(0.0f, 1.0f, (newGainDb + 24.0f) / 48.0f);
    parameter->setValueNotifyingHost(newNormalized);
    status_.setText("Applied. Input Gain is now " + juce::String(newGainDb, 1) +
                    " dB. Play again to verify.", juce::dontSendNotification);
    applyButton_.setEnabled(false);
  }

  Services& services_;
  juce::Label title_, targetLabel_, status_, measured_;
  juce::TextEditor targetDb_;
  juce::TextButton measureButton_, applyButton_, closeButton_;
  int ticks_ = 0;
  int samples_ = 0;
  double sumPower_ = 0.0;
  float peakDb_ = -120.0f;
  float measuredRmsDb_ = -120.0f;
  float targetLevelDb_ = -20.0f;
};


constexpr int kPadX = 24;
constexpr int kGroupGap = 40;  // between header items
constexpr int kPairGap = 16;   // tight pairs (undo/redo)
constexpr int kLogoWidth = 160;
constexpr int kLogoHeight = 24;  // 160 * 32 / 210, rounded like the browser
}  // namespace

// The wordmark links to tone3000.com.
class PluginHeader::LogoLink : public Clickable {
public:
  LogoLink() : Clickable({}) {
    setTitle("TONE3000");  // tone3000.com, to a screen reader
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [] { juce::URL("https://www.tone3000.com").launchInDefaultBrowser(); };
  }
  void paintButton(juce::Graphics& g, bool, bool) override {
    Brand::drawLogo(g, getLocalBounds().toFloat());
  }
};

PluginHeader::PluginHeader(Services& services)
    : services_(services),
      logo_(std::make_unique<LogoLink>()),
      presetBar_(services),
      tuner_(custom_icons::kTuningFork, 28, 18) {
  addAndMakeVisible(*logo_);
  addAndMakeVisible(presetBar_);
  addAndMakeVisible(calibrationButton_);
  calibrationButton_.setTooltip("Calibrate the signal level before the first NAM/VST3 block");
  calibrationButton_.onClick = [this] {
    auto popup = std::make_unique<InputCalibrationPopup>(services_);
    // Parent the callout to the full plugin root, not this 64px header. Using
    // the header clips the popup at the header's bottom edge (the rest sits
    // outside the parent's bounds and is never painted or clickable).
    auto* popupParent = getParentComponent();
    juce::CallOutBox::launchAsynchronously(std::move(popup), calibrationButton_.getScreenBounds(), popupParent);
  };

  vst3LibraryButton_.setTooltip(juce::String::fromUTF8("Bibliothèque de plug-ins VST3"));
  vst3LibraryButton_.onClick = [this] {
    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8("Ajouter un dossier..."));
    menu.addItem(2, juce::String::fromUTF8("Analyser / actualiser la bibliothèque"));
    menu.addSeparator();

    const auto folders = vst3library::folders(services_.prefs);
    if (folders.empty()) {
      menu.addItem(3, juce::String::fromUTF8("Aucun dossier configuré"), false);
    } else {
      menu.addSectionHeader(juce::String::fromUTF8("Dossiers de la bibliothèque"));
      for (size_t i = 0; i < folders.size(); ++i)
        menu.addItem(static_cast<int>(100 + i),
                     juce::String::fromUTF8("Retirer : ") + folders[i].getFullPathName());
    }
    menu.addSeparator();
    const auto count = vst3library::plugins(services_.prefs).size();
    menu.addItem(4, juce::String(static_cast<int>(count)) + juce::String::fromUTF8(" plug-in(s) indexé(s)"), false);

    juce::Component::SafePointer<PluginHeader> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&vst3LibraryButton_),
                       [safeThis, folders](int result) {
      if (safeThis == nullptr) return;
      if (result == 1) {
        juce::File initial;
        if (!folders.empty()) initial = folders.back();
        safeThis->vst3FolderChooser_ = std::make_unique<juce::FileChooser>(
            juce::String::fromUTF8("Choisir un dossier contenant des plug-ins VST3"), initial, "", true);
        safeThis->vst3FolderChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectDirectories,
            [safeThis](const juce::FileChooser& chooser) {
          const auto selected = chooser.getResult();
          juce::MessageManager::callAsync([safeThis, selected] {
            if (safeThis == nullptr) return;
            if (selected.isDirectory()) {
              vst3library::addFolder(safeThis->services_.prefs, selected);
              const auto found = vst3library::rescan(safeThis->services_.prefs);
              safeThis->services_.toast.show(juce::String::fromUTF8("Bibliothèque VST3 actualisée : ") +
                                               juce::String(static_cast<int>(found.size())) +
                                               juce::String::fromUTF8(" plug-in(s)."));
            }
            safeThis->vst3FolderChooser_.reset();
          });
        });
      } else if (result == 2) {
        const auto found = vst3library::rescan(safeThis->services_.prefs);
        safeThis->services_.toast.show(juce::String::fromUTF8("Bibliothèque VST3 actualisée : ") +
                                         juce::String(static_cast<int>(found.size())) +
                                         juce::String::fromUTF8(" plug-in(s)."));
      } else if (result >= 100 && static_cast<size_t>(result - 100) < folders.size()) {
        vst3library::removeFolder(safeThis->services_.prefs,
                                  static_cast<size_t>(result - 100));
        const auto found = vst3library::rescan(safeThis->services_.prefs);
        safeThis->services_.toast.show(juce::String::fromUTF8("Dossier retiré. ") +
                                         juce::String(static_cast<int>(found.size())) +
                                         juce::String::fromUTF8(" plug-in(s) dans la bibliothèque."));
      }
    });
  };
  addAndMakeVisible(vst3LibraryButton_);

  stereo_.onToggle = [this](bool stereo) {
    if (onStereoToggle) onStereoToggle(stereo);
  };
  addAndMakeVisible(stereo_);

  tuner_.setHelpText(help::text(help::Key::tuner));
  tuner_.setFillWhenActive(true);
  tuner_.onClick = [this] {
    if (onToggleTuner) onToggleTuner(!tunerShown_);
  };
  addAndMakeVisible(tuner_);

  undo_.setHelpText(help::text(help::Key::undo));
  undo_.onClick = [this] {
    if (onUndo) onUndo();
  };
  redo_.setHelpText(help::text(help::Key::redo));
  redo_.onClick = [this] {
    if (onRedo) onRedo();
  };
  addAndMakeVisible(undo_);
  addAndMakeVisible(redo_);

  account_.onOpenSettings = [this] {
    if (onOpenSettings) onOpenSettings();
  };
  account_.onLogin = [this] {
    if (onLogin) onLogin();
  };
  account_.onLogout = [this] {
    if (onLogout) onLogout();
  };
  addAndMakeVisible(account_);

  services_.chain.addListener(this);
  services_.session.addListener(this);
  sessionChanged();
  chainChanged(services_.chain.state());
  setTunerShown(false);
  setSize(design::kWidth, kHeight);
}

PluginHeader::~PluginHeader() {
  services_.session.removeListener(this);
  services_.chain.removeListener(this);
}

void PluginHeader::sessionChanged() {
  const auto& session = services_.session;
  account_.setAuthenticated(session.authenticated());
  const auto user = session.user();
  const auto url = user ? user->avatarUrl : juce::String();
  if (url == avatarUrl_ && (url.isNotEmpty() || !session.authenticated())) return;
  avatarUrl_ = url;
  account_.setAvatar(services_.images, url);
}

void PluginHeader::setTunerShown(bool shown) {
  tunerShown_ = shown;
  // Lit + HIGHLIGHT fill while the tuner is up; the icon itself stays white.
  tuner_.setActive(true);
  tuner_.setFillWhenActive(shown);
}

void PluginHeader::chainChanged(const ChainState& state) {
  undo_.setEnabled(state.canUndo);
  redo_.setEnabled(state.canRedo);
  stereo_.setStereoEnabled(state.stereoEnabled);
}

void PluginHeader::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  paint::hairlineH(g, 0, static_cast<float>(getWidth()), static_cast<float>(getHeight() - 1),
                   theme::kBorder);
}

void PluginHeader::resized() {
  // The 1px bottom border is inside the 64px box; items centre in the 63px
  // content row like the flex container did.
  auto area = getLocalBounds().withTrimmedBottom(1).reduced(kPadX, 0);
  // 31.5 in the browser; rounding up puts even-height boxes where the
  // browser's anti-aliased edges land.
  const int cy = (area.getHeight() + 1) / 2;
  auto place = [&](juce::Component& c, int right) {
    c.setBounds(right - c.getWidth(), cy - c.getHeight() / 2, c.getWidth(), c.getHeight());
    return c.getX();
  };

  logo_->setBounds(area.getX(), cy - kLogoHeight / 2, kLogoWidth, kLogoHeight);
  vst3LibraryButton_.setBounds(area.getX() + kLogoWidth + 20, cy - 15, 58, 30);
  calibrationButton_.setBounds(area.getX() + kLogoWidth + 20 + 58 + 8, cy - 15, 48, 30);

  // Right group, laid out from the right edge: account · undo/redo · tuner ·
  // stereo · presets, 40px apart (16px inside the undo/redo pair).
  int x = place(account_, area.getRight()) - kGroupGap;
  x = place(redo_, x) - kPairGap;
  x = place(undo_, x) - kGroupGap;
  x = place(tuner_, x) - kGroupGap;
  x = place(stereo_, x) - kGroupGap;
  place(presetBar_, x);
}

}  // namespace t3k::ui
