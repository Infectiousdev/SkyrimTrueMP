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
* **The pause (Journal) menu is disabled while connected**, because manual save crashed while
  the game keeps running (`UI.cpp`, kAllowList comment).
* **Hosting today means running `SkyrimTogetherServer.exe` yourself** and joining it. Our Steam
  tunnel then offers a local server to friends.

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
| D5 | Saves | Same folder for both modes vs separate profile per mode | **Undecided: needs a Windows test.** I don't know how the client treats local saves while connected. |
| D6 | `IS_MASTER` / non-default check | Keep, remove, or replace with the pack check | **Replace with the pack check; don't key behaviour off the branch name.** |
| D7 | Three closed libraries still in the client | Ask permission / replace later / replace now | **Later, one at a time, tested on your machine.** |
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

Mod candidates table (we fill this together):

| Mod | Nexus link | Category | SKSE? | Address Lib? | Co-op risk | Notes | Status |
|---|---|---|---|---|---|---|---|
| | | | | | | | |

Ideas and wishes (anything goes, we sort later):

*

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

## 9. Milestones (draft)

* **M0** Plan complete (this document).
* **M1** Windows build compiles and the game reaches the main menu.
* **M2** Launcher: link folder, checklist, launch.
* **M3** Main-menu Single player / Multiplayer; Host and Join.
* **M4** Pack: manifest from the Collection, verified by launcher and server.
* **M5** Collection published and the playthrough dry-run with two players.

## 10. Plan-complete checklist

- [ ] Vision: theme, scale and co-op shape written down (section 1)
- [ ] D1 to D8 each decided
- [ ] First version of the mod list with a co-op risk rating per mod (section 6)
- [ ] Save handling approach chosen (D5), with the Windows test it needs
- [ ] Release branch and `IS_MASTER` behaviour decided (D6)
- [ ] Launcher screens sketched: first run, checklist, error states, main menu
- [ ] Windows test plan agreed (section 8)
- [ ] Milestones ordered with what "done" is observed to mean for each

## Log

* 2026-10-05: created. Requirements R1 to R8 recorded. Single player or co-op from the main
  menu added as R3.
