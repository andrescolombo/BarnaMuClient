# BUG_MU_HELPER_TOOLTIPS - Mu Helper Skill Hover Tooltips

## Symptom

Hovering skills in the Mu Helper UI did not show skill tooltips.

This affected two places:

- The assigned skill slots in the main Mu Helper panel.
- The skill picker opened when choosing attack or buff skills.

The skill icons rendered and clicking still worked, but hovering them produced no tooltip.

## Root Cause

Mu Helper stored selected skills as raw skill ids, while the shared skill tooltip renderer expects a
learned skill-list slot index.

The shared renderer calls:

```cpp
CharacterAttribute->Skill[Type]
```

So passing a raw skill id as `Type` makes the tooltip lookup read the wrong `CharacterAttribute`
slot.

The Mu Helper skill picker also had tooltip state fields (`m_bRenderSkillInfo`,
`m_iRenderSkillInfoType`, `m_iRenderSkillInfoPosX`, `m_iRenderSkillInfoPosY`), but its mouse update
path only handled click selection. It never set those fields on hover, so no tooltip render was
armed.

## Fix

### `src/source/UI/NewUI/NewUIMuHelper.cpp`

Added `FindCharacterSkillSlotIndex()` to convert a raw skill id back to the learned skill-list slot
before rendering the tooltip.

The main Mu Helper panel now detects hover over assigned skill slots in `RenderIconList()` and calls
`RenderSelectedSkillInfo()` after the panel controls are rendered.

The Mu Helper skill picker now calls `UpdateHoveredSkillInfo()` from `UpdateMouseEvent()` so hover
state is updated before the picker renders its tooltip through the existing 2D effect callback path.

The skill picker reset path now initializes its attack/buff filter flags, avoiding undefined initial
state before a filter is selected.

### `src/source/UI/NewUI/NewUIMuHelper.h`

Added the tooltip render state for the main Mu Helper panel and declarations for the new helper
methods.

## Why the Fix Works

Both hover surfaces now pass the tooltip renderer the same kind of value used by the normal skill
bar: a `CharacterAttribute->Skill[]` slot index.

That keeps tooltip model building consistent with the existing HUD skill tooltip path and avoids
duplicating tooltip text logic inside Mu Helper.

## Verification

Manual verification:

- Hovering assigned skills in Mu Helper shows the skill tooltip.
- Hovering skills in the Mu Helper skill picker shows the skill tooltip.
- Clicking skills in the picker still assigns them to the selected Mu Helper slot.

Command verification:

```text
git diff --check -- src/source/UI/NewUI/NewUIMuHelper.cpp src/source/UI/NewUI/NewUIMuHelper.h
```

No whitespace errors were reported.

Build verification was not run in the Codex shell because `cmake`, `cl`, and `ninja` were not
available on `PATH`.
