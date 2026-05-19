# Changelog

## Unreleased

- Added an experimental inventory rearrange button that supports `1x1`, `1x3`, `2x2`, `2x3`, and `2x4` items, computes one fixed compact 8x8 layout, prefers 2-column lanes for wide items, refreshes the client-only item key after each accepted move, and uses temporary empty slots to keep moving items one at a time after a single click.

- Dimmed inventory equip-status slot overlays:
  - Red slot background still marks items for a different class.
  - Yellow slot background still marks items with missing stat requirements.
  - Overlay alpha was reduced from `0.45f` to `0.12f` to make the color much less saturated.
  - Changed lines: `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:23`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:176`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:180`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:185`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:1701`.
