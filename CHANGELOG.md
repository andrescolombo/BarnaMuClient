# Changelog

## Unreleased

- Admin minimap right-click teleport (`src/source/UI/NewUI/HUD/NewUIMiniMap.cpp`):
  - Right-clicking anywhere on the open minimap now teleports the character instantly to that tile, but only for admin/GM accounts (`CTLCODE_08OPERATOR` or `CTLCODE_20OPERATOR`). Normal players are unaffected.
  - Uses `SendInstantMoveRequest` to let the server handle the position change.
  - Shows the golden ring move-target effect and plays the click sound for feedback.
  - Admin state is latched on first detection and persists across death/respawn for the session, because `CtlCode` is reset to 0 on character re-initialization but is never re-sent by the server on respawn. The latch also checks `eBuff_GMEffect` as a secondary signal.
  - Added `#include "Network/Server/WSclient.h"` for `SocketClient`.

- Mu Helper hotkeys (`src/source/UI/NewUI/HUD/NewUIHotKey.cpp`):
  - `Z` — opens/closes the Mu Helper configuration panel (was already bound; behavior unchanged).
  - `F8` — starts/stops (play/pause) the Mu Helper without opening the panel (new binding).
  - Added `#include "MUHelper/MuHelper.h"` to `NewUIHotKey.cpp` to access `MUHelper::g_MuHelper.Toggle()`.

- Added an experimental inventory rearrange button that supports all item sizes from `1x1` to `4x4` (16 combinations), computes one fixed compact 8x8 layout, prefers width-aligned lanes for wide items, refreshes the client-only item key after each accepted move, and uses temporary empty slots to keep moving items one at a time after a single click.

- Dimmed inventory equip-status slot overlays:
  - Red slot background still marks items for a different class.
  - Yellow slot background still marks items with missing stat requirements.
  - Overlay alpha was reduced from `0.45f` to `0.12f` to make the color much less saturated.
  - Changed lines: `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:23`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:176`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:180`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:185`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:1701`.
