# Project TODO List & Tasks

This file tracks the outstanding feature requests, gameplay tweaks, and UI improvements for the MU Online client project.

---

## 1. Active Feature Backlog

### 🗺️ Minimap Navigation & Visuals
- [x] **Minimap Click-to-Move Pathfinding**
  - Implement un-rotation and coordinate-scaling math in `Check_Mouse` inside `src/source/UI/NewUI/HUD/NewUIMiniMap.cpp`.
  - Perform boundary/walkability tile checks on the 256x256 map grid.
  - Trigger character pathfinding via `PathFinding2()`, dispatch network packets via `SendMove()`, and display golden ring terrain effects.
- [x] **Minimap Monster Spawn Markers**
  - Add rendering support for monster spawn indicators on the minimap HUD.
  - Draw markers utilizing a custom texture index mapped near the standard NPC (`mini_map_ui_npc.tga`) or portal icon arrays.
  - Read spawn coordinates from a configuration file or coordinate array per map.

### 🎒 Inventory & Item Management
- [ ] **Smart Auto-Equip / Replacement Logic**
  - Refactor quick-equip or auto-pickup inventory logic (e.g. right-click or drag).
  - Currently, items only equip if the target equipment slot is empty.
  - **New Requirement**: If a new item is determined to be *better* (higher damage, defense, levels, or better options) than the currently equipped item, automatically replace the equipped item and return the old one to the player's inventory bag.

### 🤖 Mu Helper
- [x] **Mu Helper Hotkeys**
  - `Z` — open/close the Mu Helper configuration panel.
  - `F8` — start/stop (play/pause) the Mu Helper.

---

## 2. Completed / Resolved Tasks
- [x] **LLM Task Directory Map (`LLM.md`)**
  - High-density blueprint file mapping client namespaces, directories, common modding tasks, and styling guidelines.
- [x] **Minimap Clicking Math Design Guide (`docs/map_navigation_movement.md`)**
  - Mathematical reference mapping screen clicking, inverse rotation vectors, and zoom scaling offsets back to world tile grids.
- [x] **Audio System / WAV Files Copying**
  - Resolved missing sound errors on startup (`Cannot find wave file ...`) by manually populating and mapping the `Sound/` asset folders directly next to build output execution paths (`out/build/windows-x86/src/Debug/Data/Sound/`).
