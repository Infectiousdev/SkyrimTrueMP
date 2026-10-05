# Notice and attribution

SkyrimTrueMP is a **modified version of Tilted Evolution**, the multiplayer
framework behind *Skyrim Together Reborn*.

| | |
|---|---|
| Upstream project | Tilted Evolution — https://github.com/tiltedphoques/TiltedEvolution |
| Upstream authors | Tilted Phoques and contributors |
| Upstream licence | GNU General Public License v3 or later (see [`LICENSE`](LICENSE)) |
| Forked from | `dev` @ `4917189f6ac382e874f9ee735c4fef6a350e449e` (2026-10-03) |
| This fork | https://github.com/Infectiousdev/SkyrimTrueMP |

SkyrimTrueMP is **not affiliated with or endorsed by** Tilted Phoques,
Bethesda Softworks, ZeniMax or Valve. *The Elder Scrolls V: Skyrim* is a
trademark of its owners; no game assets are included in this repository.

## Licence of this fork

Everything in this repository that came from Tilted Evolution stays under the
GPLv3-or-later, and **our modifications are released under the same licence**.
Per GPLv3 §5(a), modified files are listed with dates in
[`SKYRIMTRUEMP_CHANGES.md`](SKYRIMTRUEMP_CHANGES.md). Full upstream history is
preserved in git, so authorship of every upstream line is intact
(`git log`, `git blame`).

## Components that are NOT GPL: read before redistributing binaries

The Windows client still depends on three Tilted Phoques libraries that are pulled in as
git submodules (see [`.gitmodules`](.gitmodules)):

* `Libraries/TiltedReverse` (function hooks and memory patching)
* `Libraries/TiltedHooks` (Direct3D 11, DirectInput and window hooks)
* `Libraries/TiltedUI` (the in-game browser overlay)

Each of these carries only this notice: *"Copyright (c) 2019 Tilted Phoques.
All Rights Reserved. ... Do not remove or modify any license notices."* That is
not an open-source licence and grants no right to modify or redistribute.

Two other Tilted Phoques libraries carried the same notice and have been **removed and replaced** by
original GPLv3-or-later code in this fork:

* `TiltedConnect` (networking) is replaced by `Code/net`.
* `TiltedCore` (containers, buffers, serialization, allocators) is replaced by `Code/core`. Everything
  in the project depended on it, the server included.

The server, the admin tool and the tests no longer use any closed Tilted code. See the provenance notes
in `Code/core/README.md` and `docs/SKYRIMTRUEMP.md` for exactly how the replacements were written.

How this fork handles the three that remain:

* The submodules are referenced, **never copied into or modified by this
  repository**. All SkyrimTrueMP changes live in the GPL-licensed code.
* If you distribute a *client binary built from this tree*, that binary
  contains compiled code from those three libraries. The safe course is to get
  the Tilted team's written permission first, to replace them (see
  `docs/SKYRIMTRUEMP.md`), or to distribute only the GPL parts (server, patches,
  source) and have players obtain the official Skyrim Together Reborn client.

Other dependencies (GameNetworkingSockets, EnTT, spdlog, Crypto++, …) are
fetched by xmake at build time and keep their own licences. Valve's
Steamworks SDK is **not** included or redistributed here.

## Paste-in text for a mod page / modpack

> **SkyrimTrueMP** is a modified version of **Tilted Evolution** / *Skyrim
> Together Reborn* by Tilted Phoques and contributors
> (https://github.com/tiltedphoques/TiltedEvolution), licensed under the
> GNU GPL v3 or later. Our modifications are released under the same licence.
> Complete corresponding source for this build:
> https://github.com/Infectiousdev/SkyrimTrueMP (commit `<commit-hash>`).
> Not affiliated with Tilted Phoques, Bethesda or Valve.
