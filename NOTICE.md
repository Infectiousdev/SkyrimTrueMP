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

## Components that are NOT GPL — read before redistributing binaries

The client build depends on four Tilted Phoques libraries that are pulled in as
git submodules (see [`.gitmodules`](.gitmodules)):

* `Libraries/TiltedConnect`
* `Libraries/TiltedReverse`
* `Libraries/TiltedHooks`
* `Libraries/TiltedUI`

Each of these carries only this notice: *"Copyright (c) 2019 Tilted Phoques.
All Rights Reserved. … Do not remove or modify any license notices."* That is
not an open-source licence and grants no right to modify or redistribute.

How this fork handles that:

* The submodules are referenced, **never copied into or modified by this
  repository**. All SkyrimTrueMP changes live in the GPL-licensed code.
* If you distribute a *client binary built from this tree*, that binary
  contains compiled code from those four libraries. The safe course is to get
  the Tilted team's written permission first, or to distribute only the
  GPL parts (server, patches, source) and have players obtain the official
  Skyrim Together Reborn client from its original publisher.

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
