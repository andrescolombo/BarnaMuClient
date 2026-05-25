# Visual Quality Settings — Implementation Plan

## Goal
Introduce a custom "Visual Settings" window so users can configure high-fidelity rendering at runtime:
- Mipmapping (trilinear vs bilinear filtering on world assets)
- Anisotropic Filtering (Off, 2x, 4x, 8x, 16x — hardware-clamped)
- VSync (live toggle)
- Anti-Aliasing (MSAA): config + UI scaffolding only; window-recreation deferred

## Scope & Defaults (matches `RENDER_FPS_VISUAL_QUALITY_PLAN.md` baseline)

- **Texture filter is selectively applied to world assets only.** UI atlases, fonts, and cursors stay perfectly sharp at any resolution. World scope: `BITMAP_MAPTILE`, `BITMAP_MAPGRASS`, `BITMAP_WATER`, the `BITMAP_PLAYER_TEXTURE_*` range, and any index below `BITMAP_INTERFACE_TEXTURE_BEGIN`.
- **VSync is the only live toggle.** Filter mode and anisotropy re-apply to already-loaded textures live; mipmap and MSAA changes are marked "requires restart" in the panel (see Re-application strategy).
- **Defaults preserve current look-and-feel.** Mipmap = OFF, Anisotropy = 0 (1.0f / off), VSync = ON, MSAA = OFF. Today's renderer is largely `GL_NEAREST` per the baseline doc — flipping defaults on first run would silently change every user's experience.

## Open Items (require user review)

- **Atlas + mipmap bleeding.** Many "world" textures (effects, items) are sprite atlases. Trilinear sampling on lower mips bleeds neighbors. Initial release enables mipmaps **only** for `BITMAP_MAPTILE_*`, `BITMAP_MAPGRASS_*`, `BITMAP_WATER_*`, and `BITMAP_PLAYER_TEXTURE_*` ranges. Effects and item atlases get anisotropy + bilinear but no mipmaps. Revisit after profiling.
- **NPOT padding artifacts.** `OpenJpegTurbo` and `OpenTga` upload a `NextPowerOfTwo`-padded buffer (`GlobalBitmap.cpp:634-669`) with uninitialized padding bytes. Mipmap generation on those textures pulls padding into lower levels and produces seams. Fix below in §2.
- **MSAA window-recreation deferred.** Full MSAA needs a dummy context + `wglChoosePixelFormatARB` + main window recreate. High regression risk on screen modes. This plan ships the panel, INI keys, and backend skeleton only; the recreation step is a separate change.

---

## 1. Core Configuration & Backend

### `_enum.h`
Add `INTERFACE_VISUAL_QUALITY_OPTION` to `SEASON3B::INTERFACE_LIST` immediately below `INTERFACE_OPTION`.

### `GameConfigConstants.h`
Under `CfgKeys` (consume existing `CfgSections::CfgSectionGraphics`):
```cpp
inline constexpr wchar_t CfgKeyAnisotropy[] = L"Anisotropy";
inline constexpr wchar_t CfgKeyMipmap[]     = L"Mipmap";
inline constexpr wchar_t CfgKeyVSync[]      = L"VSync";
inline constexpr wchar_t CfgKeyMSAA[]       = L"MSAA";
```
Under `CfgDefaults` (conservative — match current baseline):
```cpp
inline constexpr int  CfgDefaultAnisotropy = 0;     // 0 = off (anisotropy = 1.0f)
inline constexpr bool CfgDefaultMipmap     = false; // off — current renderer baseline
inline constexpr bool CfgDefaultVSync      = true;
inline constexpr int  CfgDefaultMSAA       = 0;     // off
```

### `GameConfig.h` / `GameConfig.cpp`
- Declare `GetAnisotropy()/SetAnisotropy(int)`, `GetMipmap()/SetMipmap(bool)`, `GetVSync()/SetVSync(bool)`, `GetMSAA()/SetMSAA(int)`.
- Private fields: `m_anisotropy`, `m_mipmap`, `m_vsync`, `m_msaa`.
- `Load()`/`Save()` use the `[Graphics]` section (`CfgSectionGraphics`) and the keys above.

---

## 2. OpenGL Texture Pipeline Refactor

### `GlobalBitmap.h`
Declare static/private helpers on `CGlobalBitmap`:
```cpp
static bool   IsWorldTexture(GLuint uiBitmapIndex);
static bool   IsMipmappableWorldTexture(GLuint uiBitmapIndex); // mipmap subset
static bool   SupportsAnisotropicFiltering();                  // cached glGetString check
static float  GetRequestedAnisotropy();                        // clamped to hw max
static void   GetEffectiveTextureFilter(GLuint idx, GLuint requested,
                                        GLuint& minFilter, GLuint& magFilter,
                                        bool& wantMipmaps);
static void   ApplyTextureParameters(GLuint idx, GLuint requested, GLuint wrapMode);
void          ReapplyAllTextureParameters(); // for live filter/aniso toggle
```

### `GlobalBitmap.cpp`

**`IsWorldTexture(idx)`** — true when index is **not** in any UI range:
```cpp
const bool inFont   = idx >= BITMAP_FONT_BEGIN   && idx <= BITMAP_FONT_END;
const bool inCursor = idx >= BITMAP_CURSOR_BEGIN && idx <= BITMAP_CURSOR_END;
const bool inUI     = idx >= BITMAP_INTERFACE_TEXTURE_BEGIN
                   && idx <= BITMAP_INTERFACE_TEXTURE_END;          // 31001..32000
return !(inFont || inCursor || inUI);
```

**`IsMipmappableWorldTexture(idx)`** — narrower, only safe-for-mipmaps ranges:
```cpp
return (idx >= BITMAP_MAPTILE_BEGIN  && idx <= BITMAP_MAPTILE_END)
    || (idx >= BITMAP_MAPGRASS_BEGIN && idx <= BITMAP_MAPGRASS_END)
    || (idx >= BITMAP_WATER_BEGIN    && idx <= BITMAP_WATER_END)
    || (idx >= BITMAP_PLAYER_TEXTURE_BEGIN && idx <= BITMAP_PLAYER_TEXTURE_END);
```
Effects, items, leaves, rain, and player-equip atlases get anisotropy + bilinear but no mipmaps to avoid atlas bleeding.

**`SupportsAnisotropicFiltering()`** — cache result of `glGetString(GL_EXTENSIONS)` substring `"GL_EXT_texture_filter_anisotropic"` in a static local.

**`GetRequestedAnisotropy()`** — read `GameConfig::GetAnisotropy()` (0/2/4/8/16), clamp to queried `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` (queried once, cached). Returns `float` (1.0f when off). Anisotropy is applied via `glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, value)` — **`f`, not `i`**.

**`GetEffectiveTextureFilter()`**
- If `!IsWorldTexture(idx)` → `min = mag = requested` (unchanged).
- Else if mipmap enabled AND `IsMipmappableWorldTexture(idx)` AND NPOT padding absent (see below) → `min = GL_LINEAR_MIPMAP_LINEAR`, `mag = GL_LINEAR`, `wantMipmaps = true`.
- Else → `min = mag = GL_LINEAR`, `wantMipmaps = false`.

**`ApplyTextureParameters()`** — centralizes MIN_FILTER, MAG_FILTER, WRAP_S, WRAP_T, and (when supported) `GL_TEXTURE_MAX_ANISOTROPY_EXT`. (The four existing `glTexParameteri` calls in `OpenJpegTurbo`/`OpenTga` are distinct parameters, not duplicates — centralize, don't "deduplicate".)

**`OpenJpegTurbo()` and `OpenTga()` changes**:
1. **Decide mipmap eligibility before upload.** Mipmaps are only enabled when `wantMipmaps == true` AND the source image is already power-of-two (`jpegWidth == textureWidth && jpegHeight == textureHeight` / `nx == Width && ny == Height`). Padded NPOT uploads skip mipmap generation to avoid edge-seam artifacts from uninitialized padding bytes.
2. If mipmaps enabled: `glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE)` **before** `glTexImage2D`. (Legacy fixed-pipeline path matches the rest of this engine's GL usage.)
3. Replace the four trailing `glTexParameteri` calls with a single `ApplyTextureParameters(uiBitmapIndex, uiFilter, uiWrapMode)`.

**`ReapplyAllTextureParameters()`** — iterates `m_mapBitmap`, binds each texture, and re-runs `ApplyTextureParameters`. Called from the panel when the user changes filter or anisotropy. Does **not** regenerate mipmaps (those are baked at upload time — Mipmap toggle is restart-only).

**Other `glTexImage2D` sites:** `ZzzInventory.cpp`, `ZzzBMD.cpp`, `UIControls.cpp` also call `glTexImage2D`. Review each; UI/inventory thumbnails should be excluded (UI sharp); BMD model textures should route through the same `ApplyTextureParameters` if they target world models.

---

## 3. VSync Backend

### `Winmain.cpp` (or a new small `Render/GL/SwapInterval.{h,cpp}`)
- Resolve `wglSwapIntervalEXT` via `wglGetProcAddress("wglSwapIntervalEXT")` **after** `wglMakeCurrent` succeeds. The symbol is not statically linked — header is in `src/dependencies/include/wglext.h`.
- Cache the function pointer; null-check before each call (driver may not expose the extension — log once and treat VSync as a no-op).
- Expose `void SetVSyncEnabled(bool)` that calls `pfn(1)` or `pfn(0)`.
- In `WinMain` after context creation, read `GameConfig::GetVSync()` and call `SetVSyncEnabled(...)`.

---

## 4. ESC / System Menu Integration

### `NewUIMessageBox.h`
Add `MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_VISUAL_OPTION` next to `MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_OPTION`.

### `NewUICustomMessageBox.h`
On `CSystemMenuMsgBox`: declare `static void VisualOptionBtnDown(...)` and `CNewUIMessageBoxButton m_BtnVisualOption`.

### `NewUICustomMessageBox.cpp`
- `Create()`: bump middle slices from 5 to 6: `height = MSGBOX_TOP_HEIGHT + (6 * MSGBOX_MIDDLE_HEIGHT) + MSGBOX_BOTTOM_HEIGHT;`
- `RenderFrame()`: `for (int i = 0; i < 6; ++i)`.
- `SetButtonInfo()`: re-derive Y offsets from the current `SetButtonInfo` math (don't hardcode 113/143/173 — read existing constants and add `MSGBOX_MIDDLE_HEIGHT` between each button). Order top-to-bottom: Option → Visual Settings → Cancel.
- `SetAddCallbackFunc()`: register click + key callbacks mapping to `MSGBOX_EVENT_USER_CUSTOM_SYSTEMMENU_VISUAL_OPTION`.
- `LButtonUp()`: detect `m_BtnVisualOption` click → dispatch event.
- `VisualOptionBtnDown()`: play button click sound, hide ESC menu, `g_pNewUISystem->Show(SEASON3B::INTERFACE_VISUAL_QUALITY_OPTION)`.
- Include `m_BtnVisualOption` in `Update()` and `RenderButtons()`.
- **Label string** uses the existing localized string table (consistent with the other System Menu entries). Do not hardcode `L"Visual Settings"` inline — add a `GlobalText` key.

---

## 5. Visual Quality Options Panel

### `NewUIVisualQualityWindow.h` (new)
- `CNewUIVisualQualityWindow : public CNewUIObj`
- Image-list enums matching the option-window style (sliced background)
- Widgets: title label, four labeled rows (Mipmap / Anisotropy / VSync / MSAA), a Close button
- `CNewUIComboBox` for Anisotropy (`Off, 2x, 4x, 8x, 16x`) and MSAA (`Off, 2x, 4x`); checkbox for Mipmap and VSync

### `NewUIVisualQualityWindow.cpp` (new)
Standard lifecycle (`Create`, `Release`, `LoadImages`, `UnloadImages`, `SetPos`, `Update`, `Render`, `SetButtonInfo`) following `NewUIOptionWindow.cpp` as the template.

**Apply behavior per setting:**
- **VSync** → live: write `GameConfig::SetVSync`, call `SetVSyncEnabled(...)`.
- **Anisotropy** → live: write config, call `CGlobalBitmap::ReapplyAllTextureParameters()`.
- **Mipmap** → write config, show inline "Restart required" hint next to the checkbox. Do NOT call reapply — mipmap generation is baked at upload.
- **MSAA** → write config, show "Restart required" hint. No GL change this phase.

Persist on toggle (no Apply/Cancel pattern, matching the option panel style).

---

## 6. UI System Registration

### `NewUISystem.h`
- Declare `CNewUIVisualQualityWindow* m_pNewVisualQualityWindow;`
- Optional convenience accessor `GetUI_NewVisualQualityWindow()`.

### `NewUISystem.cpp`
- Init `m_pNewVisualQualityWindow = nullptr` in ctor.
- In `Create()`: construct it and register with `m_pNewUIMng->AddUIObj(INTERFACE_VISUAL_QUALITY_OPTION, m_pNewVisualQualityWindow);` (mirrors `NewUIOptionWindow.cpp:116`).
- In `Release()`: safe-delete.
- Extend the `INTERFACE_OPTION` branches in `Show()` / `Hide()` / `IsVisible()` (`NewUISystem.cpp:964`, `:1393`) to also handle `INTERFACE_VISUAL_QUALITY_OPTION` with identical group/hide semantics.
- Mirror the ESC-suppression checks in `CharacterScene.cpp:414` and `LoginScene.cpp:473` so opening the panel inside login/character select behaves like the existing option window.

---

## Verification Plan

### Compilation
User builds via VS2022 (`Compilar_Release.bat` / `Compilar_Debug.bat`). **AI does not run builds.**

### Manual Verification
1. **ESC menu layout** — 6 middle slices, three buttons evenly spaced, "Visual Settings" between Option and Cancel.
2. **Panel open/close** — clicking "Visual Settings" closes ESC menu and shows the new panel; Close button hides it; ESC behavior matches Option window in login/character/main scenes.
3. **VSync live** — toggling checkbox changes FPS cap in real time (verify with the FPS HUD / GPU profiler). No restart needed. If driver lacks the extension, log line appears once and checkbox is a no-op (test by forcing a null pfn).
4. **Anisotropy live** — toggling 0 → 16x with a clean Lorencia view; distant ground texturing sharpens without a restart. Verify clamp message in log when selecting above hardware max.
5. **Mipmap restart** — toggling shows "Restart required" hint; after restart, distant maptile shimmer reduces, and **no dark seams on tile edges** (regression check on NPOT-padded textures — if seen, padding fill needs to be added; this is the known NPOT risk).
6. **Atlas check** — items, effects, NPC equipment do NOT show neighbor-sprite bleeding after mipmap is enabled (because `IsMipmappableWorldTexture` excludes their ranges).
7. **UI sharpness** — fonts, cursors, inventory icons, chat box unchanged at all settings.
8. **Persistence** — values in `config.ini` under `[Graphics]`; survive restart.
9. **MSAA** — UI selectable; restart hint shown; no GL change yet (panel + INI only this phase).

### Post-implementation
- Run `graphify update .` so the new symbols (`CNewUIVisualQualityWindow`, new GameConfig getters, `SetVSyncEnabled`, etc.) enter the graph.

---

## Out of Scope (separate change)
- Full MSAA pixel-format reconfigure: dummy window + `wglChoosePixelFormatARB` + main-context recreation. Tracked separately to isolate regression risk on display-mode switching.
- NPOT padding fix (edge-pixel replicate before upload) — needed before mipmap can safely cover non-power-of-two textures. Currently mitigated by skipping mipmaps on padded uploads.
