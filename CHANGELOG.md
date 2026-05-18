# Changelog

## Unreleased

- Dimmed inventory equip-status slot overlays:
  - Red slot background still marks items for a different class.
  - Yellow slot background still marks items with missing stat requirements.
  - Overlay alpha was reduced from `0.45f` to `0.12f` to make the color much less saturated.
  - Changed lines: `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:23`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:176`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:180`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:185`, `src/source/UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:1701`.
