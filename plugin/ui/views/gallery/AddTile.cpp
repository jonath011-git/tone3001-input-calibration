#include "AddTile.h"

#include <memory>

#include "GalleryGeometry.h"
#include "core/Help.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "services/Vst3Library.h"
#include "ExternalVst3Host.h"

namespace t3k::ui {

AddTile::Routing AddTile::routingFor(int index, int count) {
  if (count <= 1) return Routing::none;
  if (index == 0) return Routing::right;
  if (index == count - 1) return Routing::left;
  return Routing::both;
}

AddTile::AddTile(Services& services, std::string blockId, int size)
    : GalleryTile(services, std::move(blockId), size) {
  setHelpText(help::text(help::Key::addTile));
  setTitle("Add Tone");
}

AddTile::~AddTile() {
  if (vst3TypeMenu_ != nullptr) vst3TypeMenu_->close();
}

void AddTile::setRouting(Routing routing) {
  if (routing_ == routing) return;
  routing_ = routing;
  repaint();
}

void AddTile::setCanPaste(bool canPaste) { canPaste_ = canPaste; }

void AddTile::open() {
  if (onAdd) onAdd(blockId());
}

std::vector<ContextMenu::Item> AddTile::menuItems() {
  std::vector<ContextMenu::Item> items{
      {"Tone3000", Icon::Search, help::Key::addTile,
       [this] { if (onAdd) onAdd(blockId()); }},
      {"VST3 Library", Icon::File, help::Key::loadFileTile,
       [this] {
         auto files = vst3library::plugins(services().prefs);
         if (files.empty()) files = vst3library::rescan(services().prefs);

         juce::PopupMenu menu;
         if (files.empty()) {
           menu.addItem(1, juce::String::fromUTF8("Bibliothèque vide - bouton VST3 en haut pour ajouter des dossiers"), false);
           menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [](int) {});
           return;
         }

         for (size_t i = 0; i < files.size(); ++i)
           menu.addItem(static_cast<int>(i + 1), vst3library::displayName(files[i]));

         juce::Component::SafePointer<AddTile> safeThis(this);
         menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                            [safeThis, files](int result) {
           if (safeThis == nullptr || result <= 0 || static_cast<size_t>(result) > files.size()) return;
           const auto file = files[static_cast<size_t>(result - 1)];
           const auto scan = ExternalVst3Host::scanVst3File(file);
           if (!scan.ok) {
             safeThis->services().toast.show(scan.error.isNotEmpty() ? scan.error : "Couldn't inspect the VST3 plug-in.");
             return;
           }
           if (scan.plugins.empty()) {
             safeThis->services().toast.show("Aucun plug-in trouvé dans ce fichier VST3.");
             return;
           }

           // Use TONE3001's own overlay menu for shell contents. The JUCE
           // popup used for the library closes through a different menu
           // system; opening another JUCE popup from its callback caused the
           // first shell entry to load without presenting the type chooser.
           juce::MessageManager::callAsync([safeThis, file, plugins = scan.plugins] {
             if (safeThis == nullptr) return;

             std::vector<ContextMenu::Item> typeItems;
             typeItems.reserve(plugins.size());
             for (const auto& plugin : plugins) {
               juce::String label = plugin.name;
               if (plugin.manufacturerName.isNotEmpty())
                 label += " — " + plugin.manufacturerName;
               const int uniqueId = plugin.uniqueId;
               typeItems.push_back({
                   label, Icon::File, help::Key::loadFileTile,
                   [safeThis, file, uniqueId] {
                     if (safeThis == nullptr) return;
                     const auto error = safeThis->services().chain.loadExternalVst3(
                         file, safeThis->blockId(), uniqueId);
                     if (safeThis != nullptr && error.isNotEmpty())
                       safeThis->services().toast.show(error);
                   }});
             }

             if (safeThis->vst3TypeMenu_ != nullptr)
               safeThis->vst3TypeMenu_->close();
             safeThis->vst3TypeMenu_ =
                 std::make_unique<ContextMenu>(std::move(typeItems));
             safeThis->vst3TypeMenu_->openAtPoint(
                 *safeThis, safeThis->getLocalBounds().getCentre());
           });
         });
       }},
      {"VST3 Local", Icon::File, help::Key::loadFileTile,
       [this] {
         // Remember the last directory used for a VST3 so repeated inserts
         // open in the same place, across plugin-host processes and restarts.
         constexpr auto lastVst3DirectoryKey = "t3k.lastVst3Directory";
         juce::File initialDirectory(services().prefs.get(lastVst3DirectoryKey));
         if (!initialDirectory.isDirectory())
           initialDirectory = juce::File();

         vst3Chooser_ = std::make_unique<juce::FileChooser>(
             "Choose a VST3 plug-in", initialDirectory, "*.vst3");
         auto flags = juce::FileBrowserComponent::openMode |
                      juce::FileBrowserComponent::canSelectFiles |
                      juce::FileBrowserComponent::canSelectDirectories;
         // JUCE invokes the async callback from FileChooser::finished().
         // Do not destroy the chooser or trigger UI/model changes from inside
         // that callback: loading a plugin can rebuild the gallery and destroy
         // this tile, which would otherwise destroy the FileChooser while its
         // own callback is still on the stack.
         juce::Component::SafePointer<AddTile> safeThis(this);
         vst3Chooser_->launchAsync(flags, [safeThis](const juce::FileChooser& chooser) {
           const auto file = chooser.getResult();

           juce::MessageManager::callAsync([safeThis, file] {
             if (safeThis == nullptr) return;

             if (file.exists()) {
               // FileChooser returns either a VST3 module file or a .vst3
               // bundle directory. In both cases its parent is the folder
               // the user browsed to.
               const auto selectedDirectory = file.getParentDirectory();
               if (selectedDirectory.isDirectory())
                 safeThis->services().prefs.set("t3k.lastVst3Directory",
                                                selectedDirectory.getFullPathName());

               const auto error = safeThis->services().chain.loadExternalVst3(
                   file, safeThis->blockId());
               if (safeThis != nullptr && error.isNotEmpty())
                 safeThis->services().toast.show(error);
             }

             // We're now outside FileChooser::finished(), so destroying the
             // chooser here cannot invalidate its active callback stack.
             if (safeThis != nullptr)
               safeThis->vst3Chooser_.reset();
           });
         });
       }},
  };
  for (auto& item : localLoadItems()) items.push_back(std::move(item));
  items.push_back({"Paste", Icon::ClipboardPaste, help::Key::pasteBlock,
                   [this] { if (onPaste) onPaste(blockId()); }, /*disabled=*/!canPaste_});
  return items;
}

void AddTile::paint(juce::Graphics& g) {
  const auto box = getLocalBounds().toFloat();
  const bool armed = dropArmed();
  paint::fill(g, box, gallery::kTileCorner, theme::kSurfaceRaised);
  paint::dashedBorder(g, box, gallery::kTileCorner,
                      armed ? gallery::kFileDropBorder : gallery::kAddTileBorder,
                      gallery::kAddTileBorderWidth);

  if (armed) {
    const float s = gallery::kFileDropGlyphSize;
    Icons::draw(g, Icon::Upload, box.withSizeKeepingCentre(s, s), theme::kGray);
    return;
  }

  const int size = tileSize();
  const int icon = gallery::plusIconSize(size);
  // Routing lines are anchored inside the border (absolute children position
  // against the padding box), so the run to the plus ring is a border-width
  // shorter than measured from the tile's outer edge.
  if (!travelling() && routing_ != Routing::none) {
    const float run = size / 2.0f - icon / 2.0f + gallery::plusCircleInset(icon) -
                      gallery::kAddTileBorderWidth;
    const float y = box.getCentreY() - gallery::kLineWidth / 2;
    g.setColour(theme::kWhite);
    if (routing_ == Routing::left || routing_ == Routing::both)
      g.fillRect(juce::Rectangle<float>(gallery::kAddTileBorderWidth, y, run, gallery::kLineWidth));
    if (routing_ == Routing::right || routing_ == Routing::both)
      g.fillRect(juce::Rectangle<float>(box.getRight() - gallery::kAddTileBorderWidth - run, y, run,
                                        gallery::kLineWidth));
  }
  Icons::draw(g, Icon::PlusCircle, box.withSizeKeepingCentre(icon, icon), theme::kWhite,
              /*strokeWidth=*/1.0f);
}

}  // namespace t3k::ui
