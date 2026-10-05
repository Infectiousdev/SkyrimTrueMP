# SkyrimTrueMP: plan (living document)

**Status: PLANNING. Nothing in this file is built yet.** We edit it together until it is
complete, then build from it. Last updated 2026-10-05.

"Plan complete" means every box in section 10 is ticked. Until then, no code.

## 1. Vision (to be filled in together)

What we know:

* A heavily modded Skyrim SE playthrough that can be played **single player or co-op**.
* A **large** mod list, with more mods added over time.
* Co-op is **two players only** for now (more later), with Steam friends, on the same mods.
* Co-op and single player carry on in **the same world**, and saving is shared (section 6a).
* From the game's main menu you either load/start a single-player game or choose
  multiplayer.
* Installing is: link your Skyrim folder in the launcher once, and the rest is handled.
  Mods come from a **Nexus Mods Collection** (added later).
* Windows only.

Still open (we decide these in the back and forth):

* The theme and tone of the playthrough: what is the fantasy? (survival, lore-friendly
  vanilla-plus, hardcore, a story overhaul, ...)
* The exact size of the mod list. A bigger list means more things that can break co-op (section 6).

## 2. Requirements collected so far

| # | Requirement | Source |
|---|---|---|
| R1 | Windows only: no Linux build | You |
| R2 | Link the Skyrim directory once in the launcher | You |
| R3 | Single player or co-op, chosen from the main menu of one launch | You |
| R4 | Co-op only between players with identical mods | You |
| R5 | Join through Steam friends, no port forwarding | You |
| R6 | Mods delivered by a Nexus Collection (later) | You |
| R7 | Proper source and attribution so there is no licence trouble | You |
| R8 | No dependence on closed "All Rights Reserved" code in what we ship | You (reading of "use original source") |
| R9 | Mods that don't work in co-op get **compatibility work** so they end up fully compatible, bug-free and crash-free: patches, forks, or remakes (section 6b) | You |
| R10 | A large mod list, growing over time | You |
| R11 | Co-op is two players only for now; more later | You |
| R12 | One shared world; saving by one player saves for both (section 6a) | You |

## 3. What the code does today (facts, with where I read them)

* **Folder linking exists but is a bare file picker.** `Code/immersive_launcher/oobe/PathSelection.cpp`:
  asks for `SkyrimSE.exe`, suggests the Bethesda registry path, saves to
  `HKCU\Software\TiltedPhoques\TiltedEvolution\Skyrim Special Edition`. Space or `-r`
  re-asks; `--exePath <file>` sets it from a script.
* **There is no launcher app.** `SkyrimTogether.exe` maps `SkyrimSE.exe` into its own
  process with the client linked in. No window, no updater (`update/Update.cpp` is an empty
  file), no mod handling.
* **Fixed requirements, found late today.** One game version is pinned (`1.7.104.0` for Steam
  and GOG, `Code/client/main.cpp`). SKSE must be the matching AE-or-newer build in the game
  folder (`Code/client/ScriptExtender.cpp`). Address Library must be in `Data/SKSE/Plugins`.
  Each failure is a dialog at launch.
* **The mod ships as Data-level files.** CI zips `SkyrimTogetherReborn/` (exe, DLLs, UI) plus
  `GameFiles/Skyrim/*` (`SkyrimTogether.esp`, `SkyrimTogetherQuestPatches.esp`, scripts,
  creature behaviour files). My reading: that is meant to be installed as a mod. One hint:
  the folder picker re-asks if you pick a game folder that is the loader's own folder
  (`PathSelection.cpp`).
* **Single player and co-op share one process by design.** Hooks fall back to vanilla when not
  connected (for example `Games/Skyrim/Interface/UI.cpp:88`), there are 30 `IsConnected()`
  guards across the services, and on disconnect the client restores difficulty and kill-move
  settings (`PlayerService::OnDisconnected`). **I have not audited every hook**, so "single
  player is clean while disconnected" is an assumption to test on Windows.
* **The multiplayer UI is an in-game web overlay**, toggled with F2 or Right Ctrl
  (`TiltedOnlineApp::InstallHooks2`). There is **no Multiplayer button in Skyrim's own main
  menu** today.
* **A "non-default install" warning exists.** `DiscoveryService.cpp` flags any plugin list other
  than vanilla + `SkyrimTogether.esp` + `SkyrimTogetherQuestPatches.esp` after a load, but only
  when the build's git branch is named `master` (`IS_MASTER`, set in the root `xmake.lua`). It
  would fire constantly on a modlist. Several other behaviours (a debug console, some
  character-service code) also depend on `IS_MASTER`, so the release branch name matters.
* **Correction to my first draft:** while connected the client *un-pauses* a list of menus
  (inventory, magic, stats, ...) so the world keeps running. The Journal/pause menu is
  deliberately **left out of that list**, with the comment "manual save crashing while
  unpaused" (`UI.cpp:76-81`, hook at `:88`). So the Journal stays vanilla-paused and manual
  saves work normally while connected. (Checked in the code.)
* **Hosting today means running `SkyrimTogetherServer.exe` yourself** and joining it. Our Steam
  tunnel then offers a local server to friends.

### Survey of saves, sync and compatibility (2026-10-05)

A read-only code survey. I spot-checked the three claims the save design leans on
(`UI.cpp`, the delete-on-connect in `CharacterService.cpp:306-322`, and the server quest log
that is stored but never read back). The rest is the survey's reading of the code and is not
independently re-checked.

* **The server remembers nothing.** No persistence beyond a session: players are identified by
  connection id; a returning player is a brand-new character. Characters, inventories and
  quest logs live in memory only.
* **Each player keeps their own PlayerCharacter and their own Skyrim save.** Sync is *deltas
  from the moment of connecting*, largely only while in a party. The other player is a
  temporary NPC built from appearance data and deleted on disconnect.
* **Connecting uses whatever game is loaded.** No required save, no new game, and **nothing
  checks that both players loaded matching saves**. The manifest checks mods only.
* **Synced today:** actor values and health, inventory add/remove/equip, spells cast, container
  contents and door state in loaded cells, quest start/stop and stage numbers, time, weather,
  combat, movement and animation.
* **Not synced (no code found):** perks, learned spells, shouts, quest objectives/aliases/globals/
  script properties, crime and bounty, books read, discovered locations, containers and doors
  outside loaded cells, anything a mod keeps in its own scripts. Quest stages that already
  exist are not pushed to someone joining; only changes after connect travel.
* **The client deletes temporary (0xFF-range) actors on connect.** Dynamic actors a save
  contains, like summons or mod-spawned NPCs, may disappear.
* **After disconnect, the other player's changes stay in your game and your save** (quest
  stages, kills, container and door changes), so a co-op save is also a normal single-player
  save of a changed world. Known loose ends: `ObjectService::OnDisconnected` is a TODO and
  `ActorValueService::OnDisconnected` carries a comment "this crashes sometimes, no clue why".
* **Nothing hooks real saving.** There is no autosave, quicksave or new-game code in the client.
* **Mod compatibility is ad hoc: no registry, no extension point** apart from the creature
  behaviour data folder. Special cases: a launcher blocklist (EngineFixes, crash handlers,
  Fraps, SpecialK/ReShade-SpecialK, NvCamera), SkyrimSoulsRE (one flag, fragile), MO2 detection,
  SKSE, and hard-coded vanilla form ids.
* **Animation sync is keyed on behaviour-graph descriptors.** Modded graphs miss the table and
  fall back to a patching path. Nemesis/Pandora-style, DAR/OAR-style and creature-behaviour mods
  are the likely breakers.

## 4. Proposed architecture (draft)

```
Launcher app ──checks──> Skyrim folder, game version, SKSE, Address Library, pack
     │
     └─ starts ─> SkyrimTogether.exe (existing loader + client)
                        │
                        └─ Main menu ──> [Single player]  vanilla flow
                                         [Multiplayer]    Host / Join (steam:<id>)
                                                │
                                   Host: starts the server for you, friends-only + password
                                   Join: tunnel to the host's Steam ID
```

* **Pack** = a manifest of the whole mod list (Nexus mod id, file id, hash per entry). It does
  three jobs: the launcher's "is everything installed?" check, the server's reference
  manifest (instead of "first player in"), and the record of what the playthrough is.
* **Launcher** logic (path detection, version/SKSE/Address Library checks, pack comparison)
  lives in a portable library I can unit-test here. The Windows window on top is thin.
* **One load order for both modes.** Plugins load at game start, so a launch that offers both
  single player and multiplayer necessarily uses the same plugins for both.

## 5. Open decisions (recommendation first)

| # | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Install flow | A) verify on top of Vortex  B) launcher drives Vortex  C) own downloader | **A.** Nexus Collections install through Vortex and free accounts click through downloads; automating that is off the table without checking Nexus's terms. We verify, we never download or host mods. |
| D2 | Launcher shell | A) ImGui in the existing exe  B) separate small app that starts the exe via `--exePath` | **B.** The UI shouldn't depend on loader code I can't test. |
| D3 | Main-menu entry | A) a "Multiplayer" button in our overlay shown at the main menu  B) edit Skyrim's own menu | **A.** B conflicts with menu mods and is fragile. |
| D4 | Hosting | A) player runs the server  B) the game/launcher starts it when you click Host | **B.** "Click Multiplayer, click Host" is the point. |
| D5 | Saves | A) coordinated saves: each player keeps their own file, saved at the same moment  B) one host world file plus a character import for the guest | **A** (section 6a). B needs a character export/import system that doesn't exist and is the riskiest thing on the list. |
| D6 | `IS_MASTER` / non-default check | Keep, remove, or replace with the pack check | **Replace with the pack check; don't key behaviour off the branch name.** |
| D7 | Three closed libraries still in the client | Ask permission / replace later / replace now | **Later, one at a time, tested on your machine.** |
| D9 | Where the save stamp lives | A) a global in our plugin  B) a sidecar file next to the save  C) an SKSE co-save from a small plugin of ours | **Open.** Needs a look at what's cheapest to author and most robust to renamed or copied saves. |
| D10 | Solo play inside a co-op world | A) co-op saves are co-op only  B) allow it, mark the save a "branch" | **A for v1:** worlds can't be merged, so a solo branch can never rejoin. |
| D8 | Game version | Stay on the pinned version vs move | Collection must be built for the pinned version; the launcher must detect a mismatch before launching. |

## 6. Playthrough design: parking lot

How a mod interacts with co-op (my reading of how the engine works; **all of it needs testing**):

* **Probably fine:** textures, meshes, audio, shaders/ENB, UI skins that don't change menus
  the client hooks, most bug fixes.
* **Risky:** new NPCs, quests and worldspaces (sync is per actor and per quest stage);
  perks, spells and magic changes; anything that edits the player's or NPCs' stats.
* **Probably unsupported:** animation and behaviour-graph replacers (the client ships its own
  behaviour data per creature), mods that replace the player or NPC skeletons, other
  multiplayer mods, SKSE plugins that keep their own world state.
* A mod that works in single player but desyncs in co-op must be recorded, with the symptom.

### 6a. Shared world and saves (R12)

**What the engine gives us.** A Skyrim save holds exactly one player character plus the whole
changed world. In this design each player's game is a full copy of the world, and only changes
travel between the two (survey above). So there is no single "world file" to share, and the
other player's character can't simply be put inside your save.

**Three ways to read "saving for both":**

| Model | Idea | Verdict |
|---|---|---|
| **A. Coordinated saves** (recommended) | When either of you saves, both games save at the same logical moment, each into their own file. Both files are tagged as the same generation. Next time you each load your own latest generation. | Fits how the engine and client already work. Needs a save hook, a barrier protocol, a stamp and a check at connect. |
| B. One host world, guest character imported | The host's save is the world; the guest's character is exported and applied into a copy of it. | A true shared file, but needs a character export/import system for perks, spells, skills, inventory, appearance. None exists. Most crash-prone. |
| C. Copy one save to both | Whoever saves sends their file to the other. | Both players become the same character. Not viable. |

**Model A in pieces** (nothing here exists yet):

1. **A save choke point.** Route manual, auto and quick saves in a co-op session through one
   coordinated save. This means finding and hooking the engine's save call, which needs the
   game and a disassembler on Windows. It's the main unknown, and the existing "crash on manual
   save while unpaused" comment shows saves are touchy.
2. **A barrier protocol, two-phase.** Prepare, then both save, then commit only if both
   succeeded. If one fails the generation is void. **Saves are never overwritten**, so a failed
   or half-finished generation can be rolled back to the previous good one.
3. **A stamp inside each save:** session id plus generation number (D9).
4. **A check at connect.** Each client says which generation it loaded. If the two differ, the
   connection is refused with a plain explanation of which save to load. Today nothing checks
   this at all, and it's cheap, so it comes first.
5. **A desync detector.** At each barrier both clients compute a digest of the world state we
   do sync and compare. Without it, "same world" is a hope: the unsynced categories above drift
   silently, and every mod that keeps state in scripts adds more drift. It is also how we'll
   find which mods need compatibility work (6b).
6. **Starting a co-op world:** both players start a **new game together** on the pack, which
   gives an identical baseline. Taking an *existing* single-player save into co-op is not part
   of v1: it needs Model B's character import. Say so if you want it earlier.
7. **Solo and co-op saves stay separate** (D10). Co-op saves get a visible name prefix. A solo
   branch of a co-op world can't be merged back, because worlds can't be merged.

**What is shared and what isn't (to decide together).**

| | Proposal |
|---|---|
| Quests, kills, container and door state, time, weather | Shared (partly synced today) |
| Quest objectives, aliases, globals | Shared, **not synced today**: work item |
| Each player's inventory, gold, skills, perks, spells, appearance | Per player |
| Discovered locations, books read, bounty | **Your call**: shared or per player? |
| A shared stash or shared gold | **Your call** |
| Mod state kept in scripts | Case by case, see 6b |

**Costs and risks, stated plainly.**

* Large modded saves take seconds to write. Both players will see a pause at each coordinated
  save, so the plan needs an on-screen "Saving..." state for both.
* "The same world" is only as good as the sync coverage. The list will be large, so expect the
  sync and compatibility work to be the bulk of the project, not the launcher.
* Crash-on-disconnect (`ActorValueService`) and the temp-actor deletion on connect can corrupt
  or lose things around a session boundary, so they are on the stability backlog before saves.
* Everything in this section needs two Windows machines to prove.

**Order I'd build it in** (cheapest and highest value first): the stamp and connect check, then
the desync detector, then the barrier with a manual-save hook, then autosave/quicksave routing.

### 6b. Compatibility work (R9)

The goal is that every mod on the list ends up fully compatible, bug-free and crash-free in
co-op. Each mod gets a **tier**, found by testing, not guessing:

| Tier | Meaning | What we do |
|---|---|---|
| 0 | Works as is | Nothing; record the test that proved it |
| 1 | Needs a record-level fix | Our own patch plugin (ESP/ESL). It holds only our records, none of the other mod's assets |
| 2 | Needs the client to behave differently around it | A compatibility module in our client, written by us |
| 3 | Needs the mod itself changed | Fork it (open source, compatible licence), edit it (its permissions allow), or remake it ourselves |
| 4 | Can't be made to work | Replace it with an alternative |

Rules for tier 3, because you want no licence trouble:

* **First check each mod's permissions** on its Nexus page and its licence. The mod table has a
  column for it. That decides fork, edit, or remake.
* **Open source with a compatible licence:** fork it, keep the author's licence and credit.
* **Closed or "All Rights Reserved":** studying how it behaves is fine (run it, watch what it
  does, read what it exposes to scripts, write down how it interacts with the game). Our
  version is then **written by us from that behaviour spec**. Copying its code or assets, or a
  decompiled copy under another name, is not something I'll do, and it would bring back the
  same problem we just removed from the network and core libraries. Whether interoperability
  analysis of a given mod is allowed depends on its terms and your jurisdiction; if a specific
  mod matters enough, that is a question for a lawyer, not for me.
* Where the author is reachable, **asking permission** is usually quicker than a remake.

**"Done" for a compatibility item is observed, not intended.** Each one gets:

1. a written reproduction of the problem (two players, exact steps, what goes wrong),
2. an acceptance test: the same steps repeated until it passes, then a longer scripted route
   with no crash and no desync,
3. a note of which mods it was tested next to.

"Crash-free" can only be shown by running two real games. I can write the patches, the client
compatibility modules and the test scripts here; **every one of them has to be run on your
machines**, so the number of compatibility items is also a schedule decision.

**Large lists:** the list will be big, so the connect-time check has to be cheap. Planned:
hash in the background and cache results by file size and modified time, so only changed
files are re-read. Skyrim's plugin limits (254 full plugins plus ESL) cap the list as well.

### 6c. Mod candidates and ideas

**Tags** (every mod gets exactly one):

* `[MOD LIST]`: goes on the list as it is. Nothing to build.
* `[COMPAT]`: goes on the list as it is, but we build compatibility around it (client module or patch plugin).
* `[MODIFY]`: we edit or fork it (open source, or its permissions allow).
* `[REMAKE]`: we write our own version from its observed behaviour.
* `[MAKE]`: does not exist yet, we write it from scratch.

No gameplay mods have been named yet. The rows below are only what the code already requires or
special-cases. The "Licence" column stays empty until someone has read the mod's permissions.

| Tag | Mod | Nexus link | Category | SKSE? | Address Lib? | Co-op risk | Licence | Notes | Status |
|---|---|---|---|---|---|---|---|---|---|
| `[MOD LIST]` | Skyrim SE and the official DLCs | | base game | | | | | pinned version, see section 3 | required |
| `[MOD LIST]` | SKSE64, AE build matching the pinned game version | | framework | yes | | | | loaded by the client from the game folder | required |
| `[MOD LIST]` | Address Library for SKSE Plugins ("All in one") | nexusmods.com/skyrimspecialedition/mods/32444 | framework | | yes | | | client shows an error without it | required |
| ours | SkyrimTogether.esp, SkyrimTogetherQuestPatches.esp, scripts, creature behaviour files | | ships in this repo (`GameFiles/`) | | | | | part of the SkyrimTrueMP package, not a Nexus mod | required |
| `[COMPAT]` | SkyrimSoulsRE | | UI | yes | yes | medium | | client special-cases it with one fragile flag; needs a proper compat module | to review |
| `[COMPAT]` | ENB, ReShade and other graphics injectors | | graphics | | | unknown | | never tested against the client's own D3D11 hooks | to test |
| `[MODIFY]` (tentative) | SSE Engine Fixes | | engine fixes | yes | yes | high | not checked | the launcher blocks its DLL because it breaks the client's hooks, and rewrites its toml | to decide |
| `[MODIFY]` or `[REMAKE]` | Animation and behaviour replacers (Nemesis/Pandora, DAR/OAR style, creature behaviour mods) | | animation | | | high | not checked | break the client's animation sync; which ones we want is not decided | to decide |
| `[MAKE]` | Save sync: stamp, connect check, desync detector, coordinated save (6a) | | ours | maybe (D9) | | | ours | needs an engine save hook, found on Windows | planned |
| `[MAKE]` | Compat patch plugin(s) for the mod list | | ours | | | | ours | one per conflict we find (6b tier 1) | planned |
| `[MAKE]` | Client compat registry (replaces the ad hoc special cases) | | ours | | | | ours | first user: SkyrimSoulsRE | planned |
| `[MAKE]` | Main-menu Single player / Multiplayer entry (D3) | | ours | | | | ours | overlay button | planned |

**At a glance:**

* **Have:** Skyrim SE + DLCs, SKSE64, Address Library, SkyrimTogether plugins (ours).
* **Make:** save sync, compat patch plugins, client compat registry, main-menu entry.
* **Compat / modify / remake:** SkyrimSoulsRE, ENB/ReShade, Engine Fixes, animation replacers. No gameplay mods named yet.
* Not mods, but also to build: the launcher app and host-from-game.

Ideas and wishes (anything goes, we sort later):

*

### 6d. Triage card: how we go through each mod

For every mod: name and link, then answer these. The answers set the tag and the tier.

1. **What is it made of?** Only textures, meshes, sounds and plain records (likely tier 0), or does
   it add scripts, an SKSE plugin, or animation files?
2. **Does it keep state in scripts or in memory?** Script and plugin state is not synced, so each
   player's copy drifts. Per-player state is fine; world state is not.
3. **Does it touch something the client already controls?** Time and weather, menus and the
   overlay, animation graphs, quests, spells and combat, actors it creates at runtime.
4. **Does it change game settings the client enforces?** See the hard limits below.
5. **Permissions:** read the mod's permissions page and licence. That decides `[MODIFY]` or `[REMAKE]`.
6. **What test proves it works?** Two players, exact steps, expected result (6b).

**Hard limits we already know from the code** (so a mod that crosses one needs work, not luck):

* `uGridsToLoad` must be **5**: the client raises an error otherwise. Performance mods or INI
  tweaks that change it conflict.
* The **server owns the game clock and the timescale**, and weather follows the party leader.
  Mods that change time, timescale or weather logic conflict.
* **Quests sync only start/stop and stage numbers.** Objectives, aliases and globals are not
  synced, so quest mods that depend on them drift.
* **Animation sync is keyed on known behaviour graphs.** Anything that changes behaviour graphs
  or their variables falls off the table and desyncs.
* **Runtime-created (0xFF-range) actors are deleted on connect.** Mods that spawn actors
  (summons, follower frameworks, spawners) lose them.
* **The launcher blocks some injectors and overlays** (SkyrimSoulsRE is special-cased; SpecialK,
  Fraps, NvCamera, EngineFixes are blocked). ENB, ReShade and other D3D hooks are **untested
  against the client's own D3D11 hooks**: treat them as unknown until run.
* Menu mods can clash with the menus the client un-pauses and the overlay it draws.

## 7. Things I can and cannot verify (so planning stays honest)

* **Can, here:** the portable logic (detection, checks, pack diff, manifest), the server and
  network library, the protocol end to end.
* **Cannot, here:** anything that needs Windows, the game, Steam or Vortex: the launcher
  window, the registry, the client's behaviour in single player, the main-menu overlay,
  Steam P2P, and every mod interaction. Those are your test steps (section 8).

## 8. Test plan on Windows (draft)

1. First Windows build with the closed libraries in place; fix compile errors.
2. Launch to the main menu; confirm the overlay shows there.
3. Single player: new game and load a save with the client attached but disconnected.
   Watch for anything that behaves differently from vanilla.
4. Host a server, join by `127.0.0.1`, then by `steam:<id>` from a friend.
5. Disconnect and return to single player in the same session; note what is left behind.
6. Mod mismatch: a friend with one different file is refused with the reason.
7. Time the first-connect hashing on the real modlist.
8. Saves: connect with two *different* saves and confirm nothing today notices (this proves the
   gap). Then load a co-op save in single player and check what the other player's changes
   left behind.
9. Find the engine's save function and confirm manual, auto and quick saves all pass through it.
10. Crash-on-disconnect: disconnect repeatedly in each scene type and record every crash.

## 9. Milestones (draft)

* **M0** Plan complete (this document).
* **M1** Windows build compiles and the game reaches the main menu.
* **M2** Launcher: link folder, checklist, launch.
* **M3** Main-menu Single player / Multiplayer; Host and Join.
* **M4** Pack: manifest from the Collection, verified by launcher and server.
* **M4b** Shared saves: stamp and connect check, desync detector, coordinated save.
* **M4c** Compatibility work on the list, tier by tier, each with its acceptance test.
* **M5** Collection published and the playthrough dry-run with two players.

## 10. Plan-complete checklist

- [ ] Vision: theme, scale and co-op shape written down (section 1)
- [ ] D1 to D8 each decided
- [ ] First version of the mod list with a co-op risk rating per mod (section 6)
- [ ] Save approach chosen (D5, D9, D10) and the "what is shared" table filled in (6a)
- [ ] Compatibility tier process agreed (6b), including the per-mod licence column
- [ ] The first mod list is in the table with tiers guessed
- [ ] Release branch and `IS_MASTER` behaviour decided (D6)
- [ ] Launcher screens sketched: first run, checklist, error states, main menu
- [ ] Windows test plan agreed (section 8)
- [ ] Milestones ordered with what "done" is observed to mean for each

## Log

* 2026-10-05: created. Requirements R1 to R8 recorded. Single player or co-op from the main
  menu added as R3.
* 2026-10-05: R9 to R12 added (compatibility work, large list, two players, shared world and
  saves). Code survey added to section 3; corrected my misreading of the Journal menu. Sections
  6a and 6b added.
