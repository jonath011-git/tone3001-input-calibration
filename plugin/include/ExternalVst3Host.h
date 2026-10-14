#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

// Desktop-only VST3 hosting primitives for the TONE3000 chain.
//
// All discovery, instance creation/destruction and state operations must run
// off the real-time audio thread. processBlock() is the only method intended
// for the audio callback, after prepareToPlay() has completed.
class ExternalVst3Host {
public:
  struct ScanResult {
    std::vector<juce::PluginDescription> plugins;
    juce::String error;
    bool ok{false};
  };

  // Searches JUCE's standard VST3 locations. Call from a worker/message thread,
  // never from processBlock().
  static ScanResult scanInstalledVst3();

  // Creates an instance directly from a VST3 module/bundle selected by the user.
  static std::unique_ptr<ExternalVst3Host> createFromFile(
      const juce::File& file, double sampleRate, int maximumBlockSize,
      juce::PluginDescription& description, juce::String& error);

  // Creates one instance from a previously discovered description.
  // Call off the audio thread; caller owns the returned host.
  static std::unique_ptr<ExternalVst3Host> create(
      const juce::PluginDescription& description,
      double sampleRate,
      int maximumBlockSize,
      juce::String& error);

  ~ExternalVst3Host();

  // Re-prepares the wrapped plug-in for a new chain-domain rate/block size.
  // Must run off the audio thread.
  void prepare(double sampleRate, int maximumBlockSize);

  // Opens (or brings forward) the plug-in's own editor in a native popup.
  // Must be called on JUCE's message thread.
  bool showEditor(juce::String& error);

  ExternalVst3Host(const ExternalVst3Host&) = delete;
  ExternalVst3Host& operator=(const ExternalVst3Host&) = delete;

  juce::AudioProcessor* getProcessor() noexcept { return instance.get(); }
  const juce::PluginDescription& getDescription() const noexcept { return pluginDescription; }

  // Audio-thread entry point. The supplied buffer must match the configured
  // channel layout and must not exceed the prepared block size.
  void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) noexcept;

  // State is opaque plugin data. Save/restore only off the audio thread.
  juce::MemoryBlock saveState() const;
  bool restoreState(const void* data, int size, juce::String& error);

  double getLatencySeconds() const noexcept;
  int getLatencySamples() const noexcept;
  bool isValid() const noexcept { return instance != nullptr; }

private:
  class EditorWindow;

  ExternalVst3Host(juce::PluginDescription description,
                   std::unique_ptr<juce::AudioPluginInstance> plugin,
                   int maximumBlockSize);

  juce::PluginDescription pluginDescription;
  std::unique_ptr<juce::AudioPluginInstance> instance;
  // Preallocated host buffer with the exact channel count required by the
  // plug-in. It is used for both direct and mono↔stereo adaptation so the
  // audio thread never allocates and a plug-in exception cannot erase dry audio.
  juce::AudioBuffer<float> pluginBuffer;
  int mainInputChannels{0};
  int mainOutputChannels{0};
  int totalInputChannels{0};
  int totalOutputChannels{0};
  // Declared after instance so the editor window is destroyed first.
  std::unique_ptr<EditorWindow> editorWindow;
};
