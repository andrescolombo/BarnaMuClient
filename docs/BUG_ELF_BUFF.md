# BUG_ELF_BUFF - Elf Party Buff via Mu Helper

## Symptom

Elf party buffs work when cast manually, but fail when Mu Helper automation casts them.
The character plays the casting animation and switches/moves like a cast happened, but no party
member receives the buff.

Observed log after the first fix attempt:

```text
0x51 [ReceiveMuHelperStatusUpdate]
0x51 [ReceiveMuHelperStatusUpdate]
0x19 [SendRequestMagic(27 65535)]
0x19 [SendRequestMagic(27 65535)]
Received packet, size 240x19 [SendRequestMagic(28 65535)]
0x19 [SendRequestMagic(28 65535)]
Received packet, size 40x19 [SendRequestMagic(27 65535)]
Received packet, size 240x19 [SendRequestMagic(27 65535)]
0x19 [SendRequestMagic(28 65535)]
SendCheck
0x19 [SendRequestMagic(28 65535)]
Received packet, size 240x19 [SendRequestMagic(27 65535)]
0x19 [SendRequestMagic(27 65535)]
0x19 [SendRequestMagic(28 65535)]
0x19 [SendRequestMagic(28 65535)]
Received packet, size 16[MU Helper] Stopped
0x51 [ReceiveMuHelperStatusUpdate]
```

`65535` is `0xffff`, the invalid/no-target key. For targeted Elf support buffs this means the
client is still sending the buff packet without a valid player target.

## Changes Made So Far

### `src/source/MUHelper/MuHelper.cpp`

`CMuHelper::SimulateSkill()` now resolves the requested `ActionSkillType` to the learned skill-list
index before assigning `g_MovementSkill.m_iSkill`.

Reason: normal skill execution code treats `g_MovementSkill.m_iSkill` as a skill-list index when
`m_bMagic` is true. Mu Helper was previously writing the raw skill id there, which can make later
execution read the wrong skill from `CharacterAttribute->Skill[]`.

### `src/source/Engine/Object/ZzzInterface.cpp`

`AttackElf()` was changed so `AT_SKILL_HEALING`, `AT_SKILL_ATTACK`, and `AT_SKILL_DEFENSE`
families send to the selected player key when `SelectedCharacter` points at a player, falling back
to `HeroKey` only when no valid player is selected.

Reason: the original branch always used `HeroKey`, so helper-driven party support casts could play
an animation without applying to the intended party member.

## Follow-up Investigation

The latest log still shows `SendRequestMagic(27 65535)` and `SendRequestMagic(28 65535)`.
That means execution is entering a branch that derives the packet key from `g_MovementSkill.m_iTarget`
and that target is `-1`, or it is using a target index that does not resolve to a valid character key.

Next work:

- Trace all Elf support-buff paths that call `SendRequestMagic(Skill, TKey)`.
- Keep party-support targets valid even though neutral-player attack checks may intentionally clear
  PvP targets to `-1`.
- Ensure Mu Helper does not delete party-member targets from the monster target set while casting
  support buffs.

## Second Fix Attempt

The failing packet log showed:

```text
0x19 [SendRequestMagic(27 65535)]
0x19 [SendRequestMagic(28 65535)]
```

Skill `27` is `AT_SKILL_DEFENSE`; skill `28` is `AT_SKILL_ATTACK`.
The target value `65535` is `0xffff`, which comes from `UseSkillElf()` when
`g_MovementSkill.m_iTarget == -1`.

Root cause of the second failure:

- Recent PvP fixes intentionally gate Elf target assignment behind `CheckAttack()` so neutral
  player area attacks do not send a real target key without CTRL.
- Mu Helper party buffs also pass through `AttackElf()`.
- Party members are friendly targets, so `CheckAttack()` returns false and clears
  `g_MovementSkill.m_iTarget`.
- `UseSkillElf()` then sends `SendRequestMagic(Skill, 0xffff)`, so the server receives a support
  buff with no valid target.

Additional changes:

### `src/source/Engine/Object/ZzzInterface.cpp`

Added `IsElfSupportSkill()` for the Elf support families:

- `AT_SKILL_HEALING`
- `AT_SKILL_HEALING_STR`
- `AT_SKILL_ATTACK`
- `AT_SKILL_ATTACK_STR`
- `AT_SKILL_ATTACK_MASTERY`
- `AT_SKILL_DEFENSE`
- `AT_SKILL_DEFENSE_STR`
- `AT_SKILL_DEFENSE_MASTERY`

`AttackElf()` now preserves `g_MovementSkill.m_iTarget = SelectedCharacter` for these support
skills when the selected character is a player, before falling back to the normal `CheckAttack()`
gate for offensive skills.

`UseSkillElf()` now has a support-skill fallback: if `g_MovementSkill.m_iTarget` is unavailable
but `SelectedCharacter` is a valid player, it sends the selected player's key instead of `0xffff`.

Expected log after this change:

```text
0x19 [SendRequestMagic(27 <party member key>)]
0x19 [SendRequestMagic(28 <party member key>)]
```

The key should no longer be `65535` for party buff targets.
