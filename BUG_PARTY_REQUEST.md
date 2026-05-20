# BUG_PARTY_REQUEST - Mu Helper Party Request Mode

## Current Status

Unresolved.

The request mode choices save locally, but Mu Helper still fails to start from the Play button after
the party request option was added. The latest reported repro is:

1. Open Mu Helper.
2. Set Party request handling to `On`.
3. Save the Mu Helper settings.
4. Press Play.
5. Mu Helper does not start.

The client is built and tested by Andres with Visual Studio 2022.

## Intended Behavior

The Other Settings tab should handle friend, guild member, and party requests with the same three
modes:

- `On`: show the normal request popup, but do not auto accept.
- `Off`: block or reject the request automatically.
- `Auto`: accept the request automatically.

These options are local client behavior. They should not require a server edit and should not change
the server-side Mu Helper configuration packet.

## What We Were Trying to Implement

The original Mu Helper Other Settings tab had simple auto-accept checkboxes for friend and guild
member requests. The goal was to replace that binary behavior with an explicit three-mode request
policy, then extend the same policy to party invites.

The feature was meant to work like this:

- Friend requests, guild member requests, and party invites each get their own mode.
- `On` keeps the original client behavior: show the normal confirmation popup and let the player
  decide manually.
- `Off` automatically rejects the request before the popup is shown.
- `Auto` automatically accepts the request before the popup is shown.
- The setting is local to the client, because request popups are client-side behavior.
- The feature must not require game server changes.
- The feature must not change the server-owned Mu Helper hunting, obtaining, skill, or potion
  configuration packet.

## How It Was Supposed to Work

The intended implementation was split into three layers.

First, the Mu Helper UI would expose three radio-style choices per request type in the Other
Settings tab:

- Friend: `On`, `Off`, `Auto`
- Guild: `On`, `Off`, `Auto`
- Party: `On`, `Off`, `Auto`

Second, the selected modes would be saved in local client config through `GameConfig`, not through
the Mu Helper server packet. That means closing and reopening the client should remember the modes,
but pressing Save in Mu Helper should not send these local-only request choices to the server.

Third, the network receive handlers would read the local mode when an invite/request packet arrives:

- In `On` mode, do nothing special and continue into the original message box path.
- In `Off` mode, send the existing reject response packet immediately and skip the message box.
- In `Auto` mode, send the existing accept response packet immediately and skip the message box.

For party invites specifically, the intended path was:

1. `ReceiveParty()` receives the party invite packet and stores `PartyKey`.
2. `HandlePartyRequestByMuHelperMode(PartyKey)` reads `GameConfig::GetPartyRequestMode()`.
3. If the mode is `Off`, the client calls `SendPartyInviteResponse(false, PartyKey)` and returns.
4. If the mode is `Auto`, the client calls `SendPartyInviteResponse(true, PartyKey)` and returns.
5. If the mode is `On`, the client continues to the original `CPartyMsgBoxLayout` popup.

This design should have made party request handling independent from starting or stopping Mu Helper.
The current bug is that the UI/save side still appears to interfere with the Mu Helper Play flow.

## Observed Behavior

- The UI can display the request mode choices.
- The choices can be saved and loaded from local config.
- Friend, guild, and party request handlers were added client-side.
- After the party request option was added, saving the Mu Helper settings and pressing Play still
  does not start Mu Helper.

## Files Modified or Touched

### `src/source/UI/NewUI/NewUIMuHelper.cpp`

Main Mu Helper UI changes.

Touched code:

- Added request mode checkbox IDs for friend, guild, and party.
- Added request mode layout constants and labels.
- Added local helpers:
  - `ClampRequestMode()`
  - `LoadRequestModesFromConfig()`
  - `SaveRequestModesToConfig()`
  - `HasServerConfigChanged()`
- Added `_LastServerConfig` and `_HasLastServerConfig` to avoid sending a server save when only
  local request modes changed.
- Updated `CNewUIMuHelper::InitCheckBox()` to create three radio-style choices for each request
  type.
- Updated `CNewUIMuHelper::InitText()` to render `Friend`, `Guild`, and `Party` labels.
- Updated `CNewUIMuHelper::ApplyConfigFromCheckbox()` to route the new checkbox IDs.
- Updated `CNewUIMuHelper::Reset()` to default all request modes to `On`.
- Updated `CNewUIMuHelper::LoadSavedConfig()` to load local request modes after receiving the
  server Mu Helper config.
- Updated `CNewUIMuHelper::ApplyConfig()` to apply request mode checkbox states.
- Updated `CNewUIMuHelper::SaveConfig()` to save request modes locally through `GameConfig`.
- Added:
  - `CNewUIMuHelper::ApplyRequestModeBoxStates()`
  - `CNewUIMuHelper::ApplyFriendRequestMode()`
  - `CNewUIMuHelper::ApplyGuildRequestMode()`
  - `CNewUIMuHelper::ApplyPartyRequestMode()`

Outcome:

- The UI side works well enough to save and restore the choices.
- The current failure is that Mu Helper Play still does not start after saving.

### `src/source/UI/NewUI/NewUIMuHelper.h`

Added declarations for the new request mode UI helpers:

- `ApplyRequestModeBoxStates()`
- `ApplyFriendRequestMode()`
- `ApplyGuildRequestMode()`
- `ApplyPartyRequestMode()`

### `src/source/MUHelper/MuHelperData.h`

Changed request mode data in `MUHelper::ConfigData`.

Touched code:

- Added `ERequestHandlingMode`:
  - `REQUEST_HANDLING_SHOW`
  - `REQUEST_HANDLING_BLOCK`
  - `REQUEST_HANDLING_AUTO`
- Replaced the previous friend/guild auto-accept booleans with:
  - `byFriendRequestMode`
  - `byGuildRequestMode`
  - `byPartyRequestMode`

Outcome:

- This made the UI model support three states instead of a single auto-accept checkbox.
- Risk remains because these fields live inside `ConfigData`, which is also used for server Mu Helper
  save/load logic.

### `src/source/MUHelper/MuHelperData.cpp`

Touched during earlier attempts.

Attempted code:

- Tried serializing request modes into `PRECEIVE_MUHELPER_DATA::_UnusedPadding`.
- Later removed that serialization because it broke Mu Helper start.

Current code:

- Request modes are not serialized into the server Mu Helper packet.
- The current diff only removes an empty line.

Outcome:

- Writing to `_UnusedPadding` was a bad path. The server appears to expect those bytes to stay clear,
  or at least the saved blob is not safe to change there.

### `src/source/Data/GameConfig/GameConfigConstants.h`

Added local config keys:

- `CfgSectionMuHelper`
- `CfgKeyFriendRequestMode`
- `CfgKeyGuildRequestMode`
- `CfgKeyPartyRequestMode`
- `CfgDefaultRequestMode`

Outcome:

- Local config now has a place to save request modes outside the server Mu Helper packet.

### `src/source/Data/GameConfig/GameConfig.h`

Added local config accessors and fields:

- `GetFriendRequestMode()`
- `GetGuildRequestMode()`
- `GetPartyRequestMode()`
- `SetFriendRequestMode()`
- `SetGuildRequestMode()`
- `SetPartyRequestMode()`
- `m_friendRequestMode`
- `m_guildRequestMode`
- `m_partyRequestMode`

Outcome:

- Local save/load works for the three request modes.

### `src/source/Data/GameConfig/GameConfig.cpp`

Updated config load/save.

Touched code:

- `GameConfig::Load()` reads the three Mu Helper request modes.
- `GameConfig::Save()` writes the three Mu Helper request modes.
- Added the three request mode setters.

Outcome:

- The selected request mode survives closing and reopening the Mu Helper UI.

### `src/source/Network/Server/WSclient.cpp`

Added request handling behavior for incoming social packets.

Touched code:

- Added `Data/GameConfig/GameConfig.h` include.
- Added `HandlePartyRequestByMuHelperMode()`.
- Updated `ReceiveParty()` to reject, accept, or show the party request based on local mode.
- Added `SOCIAL_REQUEST_REJECTED` and `SOCIAL_REQUEST_ACCEPTED`.
- Added `HandleFriendRequestByMuHelperMode()`.
- Added `HandleGuildRequestByMuHelperMode()`.
- Updated `ReceiveGuild()` to reject, accept, or show the guild request based on local mode.
- Updated `ReceiveRequestAcceptAddFriend()` to reject, accept, or show the friend request based on
  local mode.

Outcome:

- This is where the client-side behavior should happen.
- It should be independent from starting Mu Helper, but the Play regression still happens after the
  UI/save changes.

### `src/source/MUHelper/MuHelper.cpp`

Touched during earlier repair attempts, then reverted.

Attempted code:

- Temporarily loaded/saved request modes from `GameConfig` inside `CMuHelper::Load()` and
  `CMuHelper::Save()`.
- Temporarily changed `CMuHelper::TriggerStart()` to call `Save(m_config)` before sending
  `SendMuHelperStatusChangeRequest(0)`.

Outcome:

- Saving right before Play did not fix the issue and likely interfered with starting.
- These changes were reverted.
- Current `CMuHelper::TriggerStart()` is back to only sending the status change request when the
  hero is not in a safe zone.

### `HOTKEYS.md`

Added a user-facing note that Mu Helper request handling supports:

- `On`
- `Off`
- `Auto`

Outcome:

- Documentation was updated, but the feature is not stable yet because Play still fails after save.

## Attempt History

### Attempt 1: Store modes in the Mu Helper server packet

Changed friend/guild auto-accept from booleans to three-state modes and tried storing those modes in
`PRECEIVE_MUHELPER_DATA::_UnusedPadding`.

Outcome:

- Mu Helper Play stopped working.
- Conclusion: `_UnusedPadding` must not be used for client-only settings.

### Attempt 2: Move modes to local GameConfig

Removed request mode serialization from the server packet and saved the modes to local config
instead.

Outcome:

- Choices could be saved and loaded locally.
- Incoming requests still did not block or auto accept until network receive handlers were added.

### Attempt 3: Add friend and guild request handlers

Added client-side handling in `WSclient.cpp` for friend and guild requests.

Outcome:

- Friend/guild requests now have code paths for show, block, and auto accept.
- This did not address party invites yet.

### Attempt 4: Add party request mode

Added Party to the Mu Helper UI, local config, temporary config data, and `ReceiveParty()`.

Outcome:

- Party mode saves locally.
- User reported that setting Party to `On`, saving, then pressing Play still does not start Mu
  Helper.

### Attempt 5: Save clean config before Play

Temporarily changed `CMuHelper::TriggerStart()` to save the Mu Helper config before sending the
start request.

Outcome:

- Did not fix the problem.
- The change was removed because it could race or interfere with the normal Play/start flow.

### Attempt 6: Avoid server save when only local request modes change

Added serialized server config comparison in `CNewUIMuHelper::SaveConfig()`:

- Save request modes through `GameConfig`.
- Compare the server-relevant serialized Mu Helper config before calling `g_MuHelper.Save()`.
- If only local request modes changed, call `g_MuHelper.Load(_TempConfig)` instead of sending a
  server save.

Outcome:

- Intended to avoid touching the server Mu Helper packet for local-only request settings.
- User still reports that Play does not start, so this is not confirmed as a full fix.

## Current Working Theory

The request modes are client-only settings, but they were added through `ConfigData`, which is also
used by the Mu Helper server save/load path. That creates a high risk of accidentally touching the
server helper packet or changing the save/start sequence.

The party `On` mode should behave like the original client behavior, so if `On` plus Save breaks
Play, the likely trigger is the Mu Helper save path, not the party request handler itself.

There may also be a stale server-side Mu Helper config blob from the earlier `_UnusedPadding`
attempt. Since the server cannot be edited directly, any cleanup must happen safely from the client
without blocking or racing the Play request.

## Known Bad Paths

- Do not write request modes into `PRECEIVE_MUHELPER_DATA::_UnusedPadding`.
- Do not save Mu Helper server config from `CMuHelper::TriggerStart()` immediately before Play.
- Do not require server-side edits for this feature.

## Suggested Next Debug Steps

1. Add temporary logging around `CNewUIMuHelper::SaveConfig()` to confirm whether `g_MuHelper.Save()`
   is called after changing only request modes.
2. Add temporary logging around `CMuHelper::TriggerStart()` to confirm
   `SendMuHelperStatusChangeRequest(0)` is sent when Play is pressed.
3. Add temporary logging in `ReceiveMuHelperStatusUpdate()` to confirm whether the server answers the
   Play request.
4. Dump or compare the serialized `PRECEIVE_MUHELPER_DATA` bytes before and after saving only request
   mode changes.
5. Consider removing `byFriendRequestMode`, `byGuildRequestMode`, and `byPartyRequestMode` from
   `ConfigData` completely. Store these modes only in `GameConfig` or UI-local state so they cannot
   affect the server Mu Helper config path by accident.
6. After Play works again, retest each request mode:
   - Friend: `On`, `Off`, `Auto`
   - Guild: `On`, `Off`, `Auto`
   - Party: `On`, `Off`, `Auto`
