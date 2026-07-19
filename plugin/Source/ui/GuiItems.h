// =============================================================================
//  SoundNuts II — ui/GuiItems.h
//  Custom foleys GuiItems: attractor phase scope (hero), regime strip with
//  click-to-jump, regime readout, searchable system browser with favorites,
//  LFO monitor, orbit sigil + BETA header mark, preset bar.
//  Stealth-beta: none of these render the plugin title anywhere.
// =============================================================================
#pragma once

#include <foleys_gui_magic/foleys_gui_magic.h>

namespace snutsui
{
    // Register every custom item with the WYSIWYG builder (palette + layout).
    void registerGuiItems(foleys::MagicGUIBuilder& builder);
}
