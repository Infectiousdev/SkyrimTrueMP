# SkyrimTrueMP

Co-op Skyrim **with mods**: play with Steam friends as long as everyone has the
same mods installed. SkyrimTrueMP is a fork of
[Tilted Evolution](https://github.com/tiltedphoques/TiltedEvolution) (the engine
behind Skyrim Together Reborn). See [`NOTICE.md`](../NOTICE.md) for attribution
and licensing, and [`SKYRIMTRUEMP_CHANGES.md`](../SKYRIMTRUEMP_CHANGES.md) for
exactly what differs from upstream.

**Windows only.** The game is a Windows game, so the client, the server and the
release build all target Windows x64. There is no Linux package, container image or
Linux CI. (A Linux machine was used as a *development harness* for the portable
code, see "How the code was checked" below. That is not a supported build.)

**Not compatible with official Skyrim Together Reborn.** This fork has its own
network protocol, so its client only talks to its server and vice versa. Everybody
in a session, host included, needs this build.

## Status: what was actually run

| Piece | Evidence |
|---|---|
| **Mod manifest**: wire format, SHA-256, comparison, server enforcement | Unit tests, and end to end against the real server binary (below). |
| **TrueMPNet** (our network library, replaces TiltedConnect) | Unit tests over real loopback sockets with the real GameNetworkingSockets library, mutation checks, and the real server driven end to end through it. |
| **`Code/core`** (our core library, replaces TiltedCore) | 30 unit test cases, including a CRC-64 value taken from the game's baked animation data, mutation checks, and the whole server built and run on it. |
| **Tunnel core** and **GameNetworkingSockets link** | Unit tests with real loopback UDP; the real server's handshake driven through the tunnel. |
| **Admission policy** (friends only, fail closed) and **TunnelManager** | Unit tests, including mutation checks: breaking either fail-closed rule makes tests fail. |
| **Steam P2P connections and Steam API lookup** (`ListenP2P`/`ConnectP2P`, `SteamRuntime`) | Written and cross-compiled for Windows with MinGW. **Never run against Steam.** |
| **Game client and admin tool changes** (`TransportService`, `OverlayClient`, `OverlayService`, `SteamTunnelService`, `Code/admin`) | **Not compiled and not run**: they need the game's headers and a Windows toolchain. The portable code they call is tested; `SteamTunnelService` type-checks against a stub of the game's precompiled header. |
| **The three remaining closed libraries** (`TiltedReverse`, `TiltedHooks`, `TiltedUI`) | Untouched and **unbuilt here**. They now compile against `Code/core` instead of upstream's TiltedCore; that has **not** been tested (see "Core library" below). |
| **UI** (`error.service.ts`, strings) | `tsc` and Prettier pass, with the type check shown to fail on a deliberate error. Not run inside the game's browser. |

Test run on the final tree (Linux harness): 76 test cases, 83,269 assertions, all
passed on 6 consecutive full runs.

### End-to-end run (real server)

`SkyrimTogetherServer` was started and `TrueMPProbe` (which now speaks the
TrueMPNet protocol) authenticated against it from fake Data folders. Player A
joined first and held the connection:

| Player | Result |
|---|---|
| B: identical files | accepted |
| C: one byte changed in `MyMod.esp`, same size | refused: `MyMod.esp` differs |
| D: `MyMod.esp` missing | refused: missing `MyMod.esp` |
| E: plugins reordered | refused: load order (`MyMod.esp`, `Update.esm`) |
| F: an extra plugin | refused: extra `Extra.esp` |
| G: a different SKSE DLL | refused: `SkyUI.dll` differs |
| after A left (server empty), C joined | accepted as the new reference; A was then refused |
| B, C through the tunnel | accepted / refused with the reason, same as direct |
| 2,000-plugin install | accepted (~0.22 s) |

The same scenarios were run before and after replacing TiltedConnect and TiltedCore
and gave the same results. The server log was clean and the idle server used about
4% of one core.

## What this fork adds

### Mod manifest

Upstream's check (`ModPolicy:bEnableModCheck`) compares plugin *file names* with a
`loadorder.txt` the server owner writes by hand. It ignores order, version and
content, and nothing reads the plugin files. Two players with a differently
versioned `MyMod.esp` pass it and then desync.

At connect time the client now also sends a manifest:

* every loaded plugin, **in load order**: name, size, SHA-256
* every `Data/SKSE/Plugins/*.dll`: name, size, SHA-256
* every `Data/*.bsa`: name and size only (hashing multi-GB archives on connect is
  not worth it; a size change catches nearly every version difference)

The **first player to join** becomes the reference; each later player must match
it exactly or is refused with a list of what differs. The reference is released
when the server empties. No `loadorder.txt` and no file copying. The old check
still exists and can run alongside.

Setting: `ModPolicy:bEnableManifestCheck` (default **on**).

Limits worth knowing:

* This proves the *files* are identical. It cannot make a gameplay mod that keeps
  state in Papyrus scripts or an SKSE plugin *synchronise*: each game still runs
  those locally. Visual, quest and UI mods are the realistic target.
* Loose files (meshes, textures, scripts outside archives) are not covered.
* It is a consistency check between friends, not anti-cheat: a client can send any
  manifest it likes.
* The first connect hashes every plugin on the game thread (Skyrim.esm alone is
  ~250 MB), so expect a pause of a few seconds on a large list. Results are
  cached for the rest of the session. Warming the cache in the background is not
  done yet.

### Steam friends

Neither Skyrim Together Reborn nor SkyMP can join through Steam: both need an IP
address and a forwarded port. Here a **tunnel** carries the game's UDP packets
between two players, leaving the game and the server untouched:

```
friend's game --UDP--> TunnelClient ==Steam==> TunnelHost --UDP--> STServer (host PC)
```

* The friend enters `steam:<host SteamID64>` where the IP address goes. The game
  connects to a local port once the link to the host is up.
* A player who joins a server on their own machine (`127.0.0.1`) also offers it to
  their Steam friends. The host's SteamID appears in the game log
  (`Steam friends can join this server with: steam:<id>`); there is no in-game
  display for it yet.
* Each remote player gets their own local UDP socket on the host, so the server
  sees them as separate clients. The game's own handshake, encryption and
  retransmission run end to end through the tunnel.
* **Friends only, failing closed.** If Steam cannot say whether someone is a
  friend, only SteamIDs in
  `Data/SkyrimTogetherReborn/config/steam_allowlist.txt` (one per line, `#`
  comments) are admitted. The server password still applies on top.

Risks that only a real test will settle:

* Steam's P2P connection needs either a direct path or Steam's relay. Whether
  Skyrim's AppID (489830) has relay enabled is not something I could check; without
  it, players behind strict NATs may not be able to connect.
* The lookup of Steam's interfaces from the game's own `steam_api64.dll` relies on
  exports that I could not inspect here. If they are missing the code reports
  "Steam unavailable" or falls back to the allow-list; it should not crash, but it
  has not been exercised.

### TrueMPNet (`Code/net`): our own network library

Upstream's networking lived in `TiltedConnect`, a library under an "All Rights
Reserved" notice with no licence to modify (see `NOTICE.md`). It is replaced by
`Code/net`, GPLv3-or-later, on top of Valve's open-source GameNetworkingSockets.

What it does differently:

* **Its own wire framing**: a type byte then a body, with a hard payload ceiling
  (`kMaxPayload`, a little under 512 KiB). Empty, truncated, unknown-type and
  oversize frames are dropped, never trusted. There is no protocol version number in
  the framing yet, so a client and server from different builds are not guaranteed to
  refuse each other cleanly.
* **Compression** with snappy when it actually shrinks a message (payloads under
  128 bytes are never compressed); the receiver refuses to expand a frame beyond
  `kMaxPayload`, so a hostile peer cannot make it allocate without bound.
* **Clock sync** between client and server using the minimum-round-trip sample
  (NTP style), so one slow packet cannot skew the offset.
* **A single event model.** Disconnects are queued and delivered from `Update()` on
  the caller's thread, in order, after any packets that arrived before them. Callbacks
  never run inside the socket library's own threads.
* **Kick that actually delivers the reason.** A kicked client is lingered briefly so
  the "kicked" close code reaches it before the connection is torn down.
* **Per-connection ownership** through GNS user data: a connection is always
  attributable to the server that accepted it, which makes stale-handle bugs
  detectable.
* **Own name resolver** (IPv4, IPv6, hostnames, `[::1]:port`) that runs on its own
  thread so a slow DNS lookup never blocks the game thread, and tick pacing inside
  `Server::Update()` instead of a separate timer in the caller.
* **No libuv** in the network path.

The Windows game client, the admin tool and the server runner call it through the same
small surface the old library had (`Connect`, `Send`, `Update`, callbacks, statistics),
so call sites changed very little.

Not the same as upstream, on purpose: the wire protocol is incompatible, so there is
no mixing with official Skyrim Together Reborn clients or servers.

### Core library (`Code/core`): our own base library

`TiltedCore` is also closed ("All Rights Reserved"), and *everything* depended on it:
the server, the client, the admin tool and the tests. `Code/core` replaces it with a
header-only library that keeps the `TiltedPhoques` namespace and the
`<TiltedCore/...>` include paths, so the rest of the code did not have to change.
It provides allocators (owner-tagged so memory outlives a `ScopedAllocator` scope),
STL aliases, a bit-level `Buffer`/`Reader`/`Writer`, serialization helpers, `Outcome`,
`Signal`, `TaskQueue`, `Lockable`, CRC-64 hashing and filesystem helpers. See
`Code/core/README.md` for behaviours and provenance.

Things you should know:

* **The wire format is part of the contract.** Messages are packed least-significant
  bit first, with 7-bit varints, so the encoding layer stays compatible between
  server, client and probe. The CRC-64 is the CRC-64/WE variant: the game's baked
  animation-descriptor keys only match that variant, and a unit test pins it against a
  real key and name string from the game's data.
* `FileSystem::GetPath()` returns the directory of the running executable. I could not
  confirm that matches what the old library returned in every case (for example when
  the client runs inside the game process). **Unverified.**
* Three remaining closed libraries include `<TiltedCore/...>` too. Their needed
  headers (`Hash`, `Initializer`, `Platform`, `StackAllocator`, `Stl`, `Meta`,
  `Signal`, `Filesystem`) exist in `Code/core`, and a header-compile check passed on
  MinGW, but those libraries have **not been built or run** against it.

### Provenance of the replacements

Honest account of how each replacement was written, so nobody has to guess:

* **TrueMPNet**: before deciding to replace TiltedConnect I read its `Client.cpp`,
  its public headers and its Steam interface file to learn what the callers need. The
  new library is a fresh design (framing, clock sync, event model, resolver,
  threading) and shares no code with it; the surface the callers use was preserved so
  they keep working.
* **`Code/core`**: I read the *declarations* of TiltedCore's public `Buffer`,
  `Serialization`, `Stl` and `Hash` headers to keep the interface compatible. I did
  not read its implementation `.cpp` files. The implementations are written from
  scratch, to the interface and to the wire behaviour the project's own tests and
  game data require.

Neither replacement is a clean-room in the legal sense (I had read the interface
and, for the network client, the implementation). The result is original code under
the project's GPL licence, but if clean-room provenance matters to you, get a legal
opinion rather than relying on this paragraph.

## Building and testing

### The supported build: Windows

Requirements: Visual Studio with the C++ workload, [xmake](https://xmake.io), Git
(for the submodules), and Node.js with pnpm for the in-game UI.

```
git clone --recurse-submodules https://github.com/Infectiousdev/SkyrimTrueMP
cd SkyrimTrueMP
xmake config --plat=windows --arch=x64 --mode=release --yes
xmake -y
xmake run TPTests
```

`.github/workflows/windows.yml` (called by `windows-playable-build.yml`) does this plus
the UI build (`pnpm --prefix Code/skyrim_ui/ install`, then `deploy:production`) and
produces the zip that `release.yml` publishes. **That pipeline has not been run since
these changes**, so the first Windows build is itself a test. Nothing in this repository
builds on Linux.

### The development harness (not a supported build)

The portable code (core, net, manifest, tunnel, server, probe, tests) was developed
and tested on Linux because no Windows machine was available. This is only a way of
exercising that code; it needs a few local tweaks (for example a system OpenSSL
instead of the pinned 1.1.1w source download if GitHub archive downloads are blocked)
that are deliberately **not** committed to the repository.

`TrueMPProbe` authenticates against a running server from a plain folder of plugin
files, optionally through the tunnel, so the mod policy can be exercised without Skyrim:

```
truemp_probe --connect 127.0.0.1:10578 --data <dir containing Data/> \
             --plugins Skyrim.esm,MyMod.esp [--tunnel] [--hold SECONDS]
```

The Skyrim client (`Code/client`) needs the game and Windows.

## Still closed: `TiltedReverse`, `TiltedHooks`, `TiltedUI`

Three Tilted Phoques libraries remain, as submodules, and the Windows client links
them (about 3,000 lines in total):

| Library | Role |
|---|---|
| `TiltedReverse` | Function hooks, memory patching, `ThisCall` helpers |
| `TiltedHooks` | Direct3D 11, DirectInput and window hooks |
| `TiltedUI` | In-game browser overlay (Chromium Embedded Framework) |

They are Windows-only code that talks to Direct3D, the game's memory and CEF. It cannot
be run or sensibly tested without Skyrim on a Windows machine. See `NOTICE.md` for what
that means if you distribute a client binary. Replacing them is possible but must be
done step by step against a real game; it is deliberately not done blind.

## What to test next, on Windows with two Steam accounts

1. The whole tree builds (`xmake f -m release`, `xmake build`): this has never been
   done. Expect to fix compile errors in the client, admin tool and the three closed
   libraries against `Code/core`.
2. The server starts and a client joins it directly by IP (TrueMPNet in the real game,
   not just the probe).
3. Join your own server on `127.0.0.1`: the log should say
   `Steam friends can join this server with: steam:<id>`.
4. A Steam friend on another machine enters `steam:<id>` and connects.
5. A Steam account that is *not* a friend is refused.
6. A friend with a different mod version sees the "different version" error naming the
   mod.
7. Time the first connect on a realistic mod list (the hashing pause).
