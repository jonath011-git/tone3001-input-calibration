#include "ExternalVst3Host.h"
#include "CrashDiagnostics.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {
std::shared_ptr<juce::AudioPluginFormatManager> getFormatManager() {
  // JUCE plugin instances may retain references to their creating format.
  // Keep the manager and its format objects alive for the process lifetime.
  static auto manager = [] {
    auto value = std::make_shared<juce::AudioPluginFormatManager>();
    #if JUCE_PLUGINHOST_VST3
    value->addFormat(new juce::VST3PluginFormat());
#endif
    return value;
  }();
  return manager;
}
}  // namespace

ExternalVst3Host::ScanResult ExternalVst3Host::scanInstalledVst3() {
  ScanResult result;
#if JUCE_PLUGINHOST_VST3
  auto manager = getFormatManager();
  juce::AudioPluginFormat* vst3 = nullptr;
  for (int i = 0; i < manager->getNumFormats(); ++i) {
    auto* format = manager->getFormat(i);
    if (format != nullptr && format->getName().containsIgnoreCase("VST3")) {
      vst3 = format;
      break;
    }
  }

  if (vst3 == nullptr) {
    result.error = "JUCE VST3 hosting support is not available in this build.";
    return result;
  }

  const auto locations = vst3->searchPathsForPlugins(
      vst3->getDefaultLocationsToSearch(), true, true);
  for (int i = 0; i < locations.size(); ++i) {
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, locations[i]);
    for (const auto* description : descriptions) {
      if (description != nullptr && description->pluginFormatName == vst3->getName())
        result.plugins.push_back(*description);
    }
  }

  std::sort(result.plugins.begin(), result.plugins.end(),
            [](const auto& a, const auto& b) {
              const int byName = a.name.compareIgnoreCase(b.name);
              return byName == 0 ? a.manufacturerName.compareIgnoreCase(b.manufacturerName) < 0
                                 : byName < 0;
            });
  result.ok = true;
#else
  result.error = "VST3 hosting is disabled for this platform/build.";
#endif
  return result;
}

ExternalVst3Host::ScanResult ExternalVst3Host::scanVst3File(const juce::File& file) {
  ScanResult result;
#if JUCE_PLUGINHOST_VST3
  if (!file.exists()) { result.error = "The selected VST3 file does not exist."; return result; }
  auto manager = getFormatManager();
  juce::AudioPluginFormat* vst3 = nullptr;
  for (int i = 0; i < manager->getNumFormats(); ++i) {
    auto* format = manager->getFormat(i);
    if (format != nullptr && format->getName().containsIgnoreCase("VST3")) { vst3 = format; break; }
  }
  if (vst3 == nullptr) { result.error = "VST3 hosting is not available in this build."; return result; }
  try {
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, file.getFullPathName());
    for (const auto* description : descriptions)
      if (description != nullptr && description->pluginFormatName == vst3->getName()) result.plugins.push_back(*description);
  } catch (const std::exception& e) {
    result.error = "Could not inspect the selected VST3 plug-in: " + juce::String(e.what()); return result;
  } catch (...) { result.error = "Could not inspect the selected VST3 plug-in."; return result; }
  std::sort(result.plugins.begin(), result.plugins.end(), [](const auto& a, const auto& b) {
    const int byName = a.name.compareIgnoreCase(b.name);
    return byName == 0 ? a.manufacturerName.compareIgnoreCase(b.manufacturerName) < 0 : byName < 0;
  });
  if (result.plugins.empty()) { result.error = "No VST3 plug-in could be identified in the selected file."; return result; }
  result.ok = true;
#else
  juce::ignoreUnused(file); result.error = "VST3 hosting is disabled for this platform/build.";
#endif
  return result;
}

std::unique_ptr<ExternalVst3Host> ExternalVst3Host::createFromFile(
    const juce::File& file, double sampleRate, int maximumBlockSize,
    juce::PluginDescription& description, juce::String& error, int selectedUniqueId) {
#if JUCE_PLUGINHOST_VST3
  if (!file.exists() || sampleRate <= 0.0 || maximumBlockSize <= 0) {
    error = "The selected VST3 file is invalid.";
    CrashDiagnostics::logEvent("VST3-ERROR",
        ("Rejected file or audio configuration: " + file.getFullPathName()
         + " | sampleRate=" + juce::String(sampleRate)
         + " | maxBlock=" + juce::String(maximumBlockSize)).toRawUTF8());
    return {};
  }
  auto manager = getFormatManager();
  juce::AudioPluginFormat* vst3 = nullptr;
  for (int i = 0; i < manager->getNumFormats(); ++i) {
    auto* format = manager->getFormat(i);
    if (format != nullptr && format->getName().containsIgnoreCase("VST3")) {
      vst3 = format;
      break;
    }
  }
  if (vst3 == nullptr) {
    error = "VST3 hosting is not available in this build.";
    return {};
  }
  juce::Logger::writeToLog("[VST3] Discovery start: path=" + file.getFullPathName()
                           + " | exists=" + juce::String(file.exists() ? "yes" : "no")
                           + " | file=" + juce::String(file.existsAsFile() ? "yes" : "no")
                           + " | directory=" + juce::String(file.isDirectory() ? "yes" : "no")
                           + " | size=" + juce::String(file.getSize())
                           + " | sampleRate=" + juce::String(sampleRate)
                           + " | maxBlock=" + juce::String(maximumBlockSize));
  CrashDiagnostics::logEvent("VST3", ("About to ask JUCE to load VST3: " + file.getFullPathName()).toRawUTF8());
  juce::OwnedArray<juce::PluginDescription> descriptions;
  try {
    vst3->findAllTypesForFile(descriptions, file.getFullPathName());
  } catch (const std::exception& exception) {
    error = "An exception occurred while inspecting the selected VST3 plug-in: "
            + juce::String(exception.what());
    CrashDiagnostics::logEvent("VST3-ERROR", error.toRawUTF8());
    return {};
  } catch (...) {
    error = "An unknown exception occurred while inspecting the selected VST3 plug-in.";
    CrashDiagnostics::logEvent("VST3-ERROR", error.toRawUTF8());
    return {};
  }
  juce::Logger::writeToLog("[VST3] Discovery returned " + juce::String(descriptions.size())
                           + " plugin type(s) for " + file.getFullPathName());
  if (descriptions.isEmpty()) {
    error = "No VST3 plug-in could be identified in the selected file.";
    CrashDiagnostics::logEvent("VST3-ERROR",
        ("No plugin type found for " + file.getFullPathName()).toRawUTF8());
    return {};
  }
  const juce::PluginDescription* selected = descriptions.getFirst();
  if (selectedUniqueId != 0) {
    selected = nullptr;
    for (const auto* candidate : descriptions)
      if (candidate != nullptr && candidate->uniqueId == selectedUniqueId) { selected = candidate; break; }
    if (selected == nullptr) { error = "The selected plug-in type was not found in the VST3 file. Please rescan the library."; return {}; }
  }
  description = *selected;
  return create(description, sampleRate, maximumBlockSize, error);
#else
  juce::ignoreUnused(file, sampleRate, maximumBlockSize, description);
  error = "VST3 hosting is disabled for this platform/build.";
  return {};
#endif
}

std::unique_ptr<ExternalVst3Host> ExternalVst3Host::create(
    const juce::PluginDescription& description,
    double sampleRate,
    int maximumBlockSize,
    juce::String& error) {
#if JUCE_PLUGINHOST_VST3
  if (sampleRate <= 0.0 || maximumBlockSize <= 0) {
    error = "Invalid sample rate or maximum block size.";
    return {};
  }

  auto manager = getFormatManager();
  juce::AudioPluginFormat* format = nullptr;
  for (int i = 0; i < manager->getNumFormats(); ++i) {
    auto* candidate = manager->getFormat(i);
    if (candidate != nullptr &&
        candidate->getName() == description.pluginFormatName &&
        candidate->getName().containsIgnoreCase("VST3")) {
      format = candidate;
      break;
    }
  }
  if (format == nullptr) {
    error = "The selected plug-in is not a supported VST3 plug-in.";
    return {};
  }

  auto instance = manager->createPluginInstance(description, sampleRate,
                                                maximumBlockSize, error);
  if (instance == nullptr)
    return {};

  instance->setNonRealtime(false);
  instance->prepareToPlay(sampleRate, maximumBlockSize);
  return std::unique_ptr<ExternalVst3Host>(
      new ExternalVst3Host(description, std::move(instance)));
#else
  juce::ignoreUnused(description, sampleRate, maximumBlockSize);
  error = "VST3 hosting is disabled for this platform/build.";
  return {};
#endif
}

class ExternalVst3Host::EditorWindow final : public juce::DocumentWindow {
public:
  EditorWindow(const juce::String& title, juce::AudioProcessorEditor* editor)
      : juce::DocumentWindow(title,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 juce::ResizableWindow::backgroundColourId),
                             juce::DocumentWindow::closeButton) {
    jassert(editor != nullptr);
    const int width = juce::jmax(320, editor->getWidth());
    const int height = juce::jmax(200, editor->getHeight());
    setUsingNativeTitleBar(true);
    setResizable(editor->isResizable(), false);
    setContentOwned(editor, true);
    centreWithSize(width, height);
  }

  void closeButtonPressed() override {
    // Keep the editor instance alive; clicking the block again reopens it.
    setVisible(false);
  }
};

ExternalVst3Host::ExternalVst3Host(
    juce::PluginDescription description,
    std::unique_ptr<juce::AudioPluginInstance> plugin)
    : pluginDescription(std::move(description)), instance(std::move(plugin)) {}

void ExternalVst3Host::prepare(double sampleRate, int maximumBlockSize) {
  if (instance == nullptr || sampleRate <= 0.0 || maximumBlockSize <= 0)
    return;
  instance->releaseResources();
  instance->prepareToPlay(sampleRate, maximumBlockSize);
}

ExternalVst3Host::~ExternalVst3Host() {
  // The editor references the processor, so destroy the popup before releasing
  // or destroying the wrapped instance. Both operations stay off the audio thread.
  editorWindow.reset();
  if (instance != nullptr)
    instance->releaseResources();
}

bool ExternalVst3Host::showEditor(juce::String& error) {
  auto* messageManager = juce::MessageManager::getInstance();
  if (messageManager == nullptr || !messageManager->isThisTheMessageThread()) {
    error = "The VST3 editor can only be opened on the UI thread.";
    return false;
  }
  if (instance == nullptr) {
    error = "The VST3 plug-in is no longer loaded.";
    return false;
  }

  if (editorWindow != nullptr) {
    editorWindow->setVisible(true);
    editorWindow->toFront(true);
    return true;
  }

  if (!instance->hasEditor()) {
    error = "This VST3 plug-in does not provide a graphical editor.";
    return false;
  }

  auto* editor = instance->createEditorIfNeeded();
  if (editor == nullptr) {
    error = "JUCE could not create the VST3 plug-in's graphical editor.";
    return false;
  }

  editorWindow = std::make_unique<EditorWindow>(pluginDescription.name, editor);
  editorWindow->setVisible(true);
  editorWindow->toFront(true);
  return true;
}

void ExternalVst3Host::processBlock(juce::AudioBuffer<float>& audio,
                                    juce::MidiBuffer& midi) noexcept {
  if (instance == nullptr)
    return;
  try {
    instance->processBlock(audio, midi);
  } catch (...) {
    // Never let a third-party exception unwind through the host audio callback.
    audio.clear();
    midi.clear();
  }
}

juce::MemoryBlock ExternalVst3Host::saveState() const {
  juce::MemoryBlock state;
  if (instance != nullptr)
    instance->getStateInformation(state);
  return state;
}

bool ExternalVst3Host::restoreState(const void* data, int size, juce::String& error) {
  if (instance == nullptr || size < 0 || (size > 0 && data == nullptr)) {
    error = "Cannot restore state for an invalid plug-in instance or state buffer.";
    return false;
  }
  instance->setStateInformation(data, size);
  return true;
}

double ExternalVst3Host::getLatencySeconds() const noexcept {
  if (instance == nullptr || instance->getSampleRate() <= 0.0)
    return 0.0;
  return static_cast<double>(instance->getLatencySamples()) / instance->getSampleRate();
}

int ExternalVst3Host::getLatencySamples() const noexcept {
  return instance != nullptr ? instance->getLatencySamples() : 0;
}
