# Render FPS and Visual Quality Plan

## Goal

Improve the client image quality without causing large FPS drops. Changes should be measured one at a time, with a fast rollback path for anything that hurts performance.

## Baseline First

1. Pick repeatable test scenes:
   - Login scene.
   - Busy town scene.
   - Combat scene with effects.
   - Empty map area.
2. Record current FPS, frame pacing, GPU usage, and CPU usage in each scene.
3. Keep screenshots for before/after comparisons.
4. Treat the current non-MSAA renderer as the performance baseline.

## High-Risk Option: Full-Scene MSAA

Full-scene MSAA can improve polygon edges, but the previous test showed a major FPS drop: about 51 FPS to 19 FPS. Do not enable it by default.

Possible future use:
1. Make MSAA optional in config only.
2. Limit choices to Off, 2x, and 4x.
3. Default to Off.
4. Show a warning or auto-disable if FPS drops heavily.

## Lower-Risk Visual Improvements

### Texture Filtering Review

Many textures still load with `GL_NEAREST`, especially map tiles and effects. Review texture groups individually:

1. UI textures: usually keep sharp or use existing `GL_LINEAR` where already intended.
2. Terrain textures: test `GL_LINEAR` on selected maps.
3. Effects and particles: test `GL_LINEAR` where pixel shimmer is visible.
4. Item/model textures: test cautiously because filtering can change the classic visual style.

Expected risk: low to medium. This may improve shimmer and blockiness with much less cost than MSAA.

### Mipmaps for Terrain and World Textures

Mipmaps can reduce distant texture shimmer and sometimes improve performance by sampling smaller texture levels.

Plan:
1. Add mipmap generation only for world/terrain textures first.
2. Avoid mipmaps for UI atlases and text.
3. Compare distant terrain shimmer and memory usage.
4. Check old GPUs and drivers carefully.

Expected risk: medium. Can improve quality and sometimes FPS, but may blur textures if not tuned.

### Anisotropic Filtering

Anisotropic filtering improves angled ground textures, especially terrain viewed from the game camera.

Plan:
1. Detect `GL_EXT_texture_filter_anisotropic`.
2. Add optional levels: Off, 2x, 4x, 8x.
3. Default to Off or 2x after testing.
4. Apply only to terrain/world textures first.

Expected risk: low to medium. Usually cheaper than MSAA and useful for this camera angle.

### Alpha-Tested Edges

Some jagged edges may come from alpha-tested textures such as vegetation, wings, effects, or UI cutouts. MSAA does not always solve alpha-test aliasing well.

Plan:
1. Identify the worst alpha-tested textures.
2. Test alpha threshold tuning in isolated render paths.
3. Consider alpha blending for selected assets if depth sorting remains correct.

Expected risk: medium. Can improve edges, but may affect sorting and visual correctness.

### Resolution and UI Scaling

Some visual roughness may come from scaling old 640x480 UI coordinates to modern window sizes.

Plan:
1. Audit text and bitmap scaling paths.
2. Keep UI pixel alignment stable.
3. Avoid global smoothing that blurs text.
4. Improve only the worst screens first.

Expected risk: medium. Good results need targeted fixes rather than one global switch.

## FPS Improvements to Investigate

### State Change Reduction

The renderer uses many immediate-mode OpenGL calls and global state changes. Some helpers already cache texture/state, but there may be avoidable churn.

Plan:
1. Profile hot render paths.
2. Count texture binds, blend mode changes, and depth/cull toggles.
3. Optimize repeated state changes only where measured.

### Particle and Effect Budgets

Effects can dominate busy scenes.

Plan:
1. Add debug counters for active particles, joints, sprites, and effects.
2. Identify the worst scenes.
3. Add optional quality levels for expensive effects.

### Terrain and Object Culling

The camera and editor already have culling controls. Use those systems to tune quality/performance tradeoffs.

Plan:
1. Compare cull distances per map.
2. Look for objects rendered outside useful view.
3. Keep defaults conservative and expose tuning only where useful.

## Recommended Order

1. Measure baseline scenes.
2. Test texture filtering on terrain/world textures.
3. Test mipmaps for terrain/world textures.
4. Test anisotropic filtering.
5. Investigate alpha-tested edge issues.
6. Profile state changes and effect counts.
7. Only revisit MSAA as an optional setting, never as default.

## Acceptance Criteria

1. No default setting should drop FPS by more than 5 percent in baseline scenes.
2. Optional quality settings must be configurable and easy to disable.
3. Any visual improvement should include before/after screenshots.
4. Changes should be small and isolated so bad experiments can be reverted quickly.
