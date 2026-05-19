# Server-Side Fix Instructions: PvP Viewport State (OpenMU)

**Target reader:** the next Claude session working on the **OpenMU server** repo (separate from this client repo).
**Related client work:** marker `[BUG_CTRL_PVP]` in `src/source/Network/Server/WSclient.cpp` and the `## Unreleased` entry in `CHANGELOG.md` of this client repo. Full analysis: `BUG_CTRL_PVP.md` at the client repo root.

## Problem in one paragraph

When a client enters the world (cold login, relog, or character switch) and another player is already visible, the OpenMU server sends the `0x12` viewport-create packet for that player but does **not** reliably send the matching `0x65` `GuildIDViewport` packet and `0xF8 0x05` `GensInfluenceViewport` packet in a guaranteed order or at all. The client's viewport-create handler resets guild / Gens / admin fields to defaults, so those follow-up packets are required to restore correct PvP state. When they don't arrive (or arrive before viewport-create), `CheckAttack()` / `getTargetCharacterKey()` on the client see neutral defaults and refuse to acknowledge valid PvP targets unless `CTRL` is held. Symptom: Elf Triple Shot / Multi Shot, Summoner Lightning Shock, Wizard Decay, etc. cannot hit other players without `CTRL` until the targets leave and re-enter viewport.

A client-side **workaround** is already live (snapshots PvP fields by `Key` before viewport-create and restores them after) but it can preserve stale state if guild/Gens legitimately change mid-session. The server fix below is the proper solution and lets the client workaround be removed.

## Packet opcodes (verified in client dispatcher)

| Opcode | Direction | Handler in client | Purpose |
|---|---|---|---|
| `0x12` | S → C | `ReceiveCreatePlayerViewportExtended` ([WSclient.cpp:2482](../src/source/Network/Server/WSclient.cpp#L2482)) | Create / update visible player |
| `0x65` | S → C | `ReceiveGuildIDViewport` ([WSclient.cpp:7660](../src/source/Network/Server/WSclient.cpp#L7660)) | Guild mark, status, type, relationship |
| `0xF8` sub `0x05` | S → C | `ReceiveOtherPlayerGensInfluenceViewport` ([WSclient.cpp:10424](../src/source/Network/Server/WSclient.cpp#L10424)) | Gens faction / class / contribution for visible players |
| `0xF8` sub `0x07` | S → C | `ReceivePlayerGensInfluence` ([WSclient.cpp:10414](../src/source/Network/Server/WSclient.cpp#L10414)) | Hero's own Gens data |

## Fields wiped client-side on every `0x12`

`CreateCharacterPointer()` ([ZzzCharacter.cpp:11524](../src/source/Engine/Object/ZzzCharacter.cpp#L11524)) resets:
`GuildMarkIndex`, `GuildStatus`, `GuildType`, `GuildRelationShip`, `GuildTeam`, `GuildMasterKillCount`, `EtcPart`, `m_byGensInfluence`, `CtlCode`.
`PK` is **not** an issue — it travels inside the `0x12` packet itself (`RotationAndHeroState & 0xf`).

## What the server fix needs to guarantee

For every `0x12` viewport-create packet the server emits for a player, one of the following must be true:

1. **Preferred — bundled:** the same `0x12` packet carries the guild ID / status / type / relationship and the Gens influence fields inline. This needs a protocol extension (new fields in the extended viewport-create packet). Bump a protocol-version field if the client treats unknown trailing bytes as malformed.
2. **Acceptable — guaranteed follow-up:** immediately after every `0x12`, queue the matching `0x65` for that player and the `0xF8 0x05` Gens entry for that player. Ordering rule on the wire: `0x12` first, then `0x65`, then `0xF8 0x05`. Never send the follow-ups before the `0x12`.

Either approach must apply in **all** of these flows (this is where the current bug lives):

- Cold login / character enters world for the first time and queries the viewport.
- Relog of the same character.
- Map change / teleport / warp that rebuilds the viewport.
- A new player enters the viewport range of an already-online player (other direction is usually fine, but verify).
- Character class switch.

## Where to look in the OpenMU codebase

This is the C# server. Search hints (names will vary by version):

- Packet builders for the extended viewport packet, by name (`AddCharactersToScope`, `NewPlayersInScopePlugIn`, `CreateExtendedCharacter`, `ObjectsOutOfScope`, etc.).
- Plug-ins / message handlers that subscribe to scope/viewport events. Look for anything implementing or invoking `INewPlayersInScopePlugIn` and similar.
- Guild plug-ins (`AssignCharactersToGuildPlugIn`, `GuildInfoResponsePlugIn`, anything that sends the guild viewport reply).
- Gens / battle-castle / event subsystems that send influence updates.

When you find the place that emits the `0x12` viewport-create, that's where the bundled / follow-up sends must be added.

## Test plan

The client has built-in diagnostic logging guarded by `_DEBUG`. Use a Debug build of the client at `out/build/windows-x86/src/Debug/Main.exe`. Two log files appear next to `Main.exe`:

- `PvpViewportState.log` — written from `ReceiveCreatePlayerViewportExtended`, `ReceivePK`, `ReceiveGuildIDViewport`. Each line tags `event=create-player-viewport | pk-update | guild-viewport` plus `pk`, `guildTeam`, `guildRelation`, `guildStatus`, `guildType`, `guildMark`, `gens`, `party`, `duelEnemy`.
- `AreaSkillTarget.log` — written from the shared area-skill send path. Each line tags `skill`, `selected`, `sentTargetKey`, `checkAttack`, `ctrl`, plus the target's PvP fields at send time.

**Before the server fix**, on a freshly-entered client the typical log pattern is:

```
[PvpViewportState] event=create-player-viewport ... guildTeam=0 guildRelation=0 guildStatus=255 guildMark=-1 gens=0
(no guild-viewport line follows for that Key)
[AreaSkillTarget] skill=<multi/triple/lightning> selected=<n> sentTargetKey=65535 checkAttack=0 ctrl=0
```

**After the server fix**, the same client run should show:

```
[PvpViewportState] event=create-player-viewport ... (initial state)
[PvpViewportState] event=guild-viewport key=<same> ... guildRelation=<actual> guildMark=<actual>
[AreaSkillTarget] skill=<multi/triple/lightning> selected=<n> sentTargetKey=<real key> checkAttack=1 ctrl=0
```

Steps:

1. Two characters on two clients. Caster on a Debug build.
2. Caster cold-logs into a map where the target is already visible.
3. Try area skill **without** holding `CTRL`. Should hit immediately on first try.
4. Repeat with caster relog.
5. Repeat with caster character-switch.
6. Repeat with caster teleporting/warping into a map where the target is already standing.
7. Repeat the same scenarios on a Gens / Strife map (Vulcanus, Karutan, etc.) — the Gens follow-up path is exercised here.

## Once the server fix ships

Once test logs above show `guild-viewport` (and Gens viewport on Strife maps) always following `create-player-viewport`, the client workaround can be safely removed:

- Delete the two `[BUG_CTRL_PVP]` blocks in `src/source/Network/Server/WSclient.cpp` (around lines 2495–2540 and 2606–2623).
- Remove or downgrade the matching `## Unreleased` entry in this repo's `CHANGELOG.md` — replace with a note that the server now ships state with viewport-create.
- Optionally drop the debug-only `PvpViewportState.log` / `AreaSkillTarget.log` helpers if no longer needed.

## Cross-references

- Full bug analysis with test logs: [`BUG_CTRL_PVP.md`](../BUG_CTRL_PVP.md) (client repo root)
- Client-side workaround marker: `[BUG_CTRL_PVP]` grep
- Client changelog entry: [`CHANGELOG.md`](../CHANGELOG.md) under `## Unreleased`
