#include "MainScreen.h"

namespace t3k::ui {

MainScreen::MainScreen(Services& services)
    : services_(services),
      spreadEnabled_(services.backend, "spreadEnabled"),
      inputMeter_(services.meters, /*input=*/true, kMeterHeight, DbMeter::Labels::left),
      outputMeter_(services.meters, /*input=*/false, kMeterHeight, DbMeter::Labels::right),
      chain_(services) {
  // The meters sit above the chain (its gutter fades), so stereo columns
  // overflowing into the centre aren't covered by it.
  addAndMakeVisible(chain_);
  addAndMakeVisible(inputMeter_);
  addAndMakeVisible(outputMeter_);

  spreadEnabled_.onChange = [this] { syncMeters(); };
  services_.chain.addListener(this);
  services_.prefs.addListener(this);
  loadBackground();
  syncMeters();
}

MainScreen::~MainScreen() {
  services_.prefs.removeListener(this);
  services_.chain.removeListener(this);
}

void MainScreen::prefChanged(const juce::String& key) {
  if (key == "t3k.backgroundImage") loadBackground();
}

void MainScreen::loadBackground() {
  const auto path = services_.prefs.get("t3k.backgroundImage");
  const juce::File file(path);
  backgroundImage_ = path.isNotEmpty() && file.existsAsFile()
                         ? juce::ImageFileFormat::loadFrom(file)
                         : juce::Image{};
  repaint();
}

void MainScreen::paint(juce::Graphics& g) {
  g.fillAll(juce::Colours::black);
  if (!backgroundImage_.isValid()) return;

  // Cover the whole central band without distorting the image. Crop only
  // the excess dimension when the image and the window have different ratios.
  const auto dest = getLocalBounds().toFloat();
  const float imageW = static_cast<float>(backgroundImage_.getWidth());
  const float imageH = static_cast<float>(backgroundImage_.getHeight());
  const float destRatio = dest.getWidth() / juce::jmax(1.0f, dest.getHeight());
  const float imageRatio = imageW / juce::jmax(1.0f, imageH);
  float sourceX = 0.0f, sourceY = 0.0f, sourceW = imageW, sourceH = imageH;
  if (imageRatio > destRatio) {
    sourceW = imageH * destRatio;
    sourceX = (imageW - sourceW) * 0.5f;
  } else {
    sourceH = imageW / destRatio;
    sourceY = (imageH - sourceH) * 0.5f;
  }
  g.drawImage(backgroundImage_, 0, 0, getWidth(), getHeight(),
              juce::roundToInt(sourceX), juce::roundToInt(sourceY),
              juce::roundToInt(sourceW), juce::roundToInt(sourceH));
}

void MainScreen::chainChanged(const ChainState&) { syncMeters(); }

void MainScreen::syncMeters() {
  const auto& chain = services_.chain.state();
  // Two input bars whenever both source channels reach the chain (Stereo or
  // Dual Mono); the L/R picks fold onto one.
  inputMeter_.setStereo(chain.stereoInput && (chain.inputMode == InputMode::stereo ||
                                              chain.inputMode == InputMode::dualMono));
  // The output carries a real stereo image only when a stereo-image feature
  // is on (stereo mode, mono-mode spread, or dual mono's two voices) AND the
  // rig can reproduce it (dualMonoActive already implies that).
  outputMeter_.setStereo(((chain.stereoEnabled || spreadEnabled_.boolValue()) && chain.stereoOutput) ||
                         chain.dualMonoActive);
}

void MainScreen::resized() {
  auto area = getLocalBounds().reduced(kPadX, 0);
  // Each meter slot is the mono footprint; the component is wider by kInset
  // on both sides to hold the stereo overflow.
  const int meterY = (getHeight() - inputMeter_.getHeight()) / 2;
  inputMeter_.setTopLeftPosition(area.getX() - DbMeter::kInset, meterY);
  outputMeter_.setTopLeftPosition(area.getRight() - DbMeter::kFootprint - DbMeter::kInset, meterY);
  area.removeFromLeft(DbMeter::kFootprint);
  area.removeFromRight(DbMeter::kFootprint);
  chain_.setBounds(area);
}

}  // namespace t3k::ui
