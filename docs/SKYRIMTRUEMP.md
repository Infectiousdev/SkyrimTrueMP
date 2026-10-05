# SkyrimTrueMP

Co-op Skyrim **with mods**: play with Steam friends as long as everyone has the
same mods installed. SkyrimTrueMP is a fork of
[Tilted Evolution](https://github.com/tiltedphoques/TiltedEvolution) (the engine
behind Skyrim Together Reborn). See [`NOTICE.md`](../NOTICE.md) for attribution
and licensing, and [`SKYRIMTRUEMP_CHANGES.md`](../SKYRIMTRUEMP_CHANGES.md) for
exactly what differs from upstream.

## Status: what was actually run

| Piece | Evidence |
|---|---|
| **Mod manifest**: wire format, SHA-256, comparison, server enforcement | Unit tests, and end to end on Linux against the real server binary (below). |
| **Tunnel core** and **GameNetworkingSockets link** | Unit tests with real loopback UDP and the real standalone library; and the real server's handshake driven through the tunnel (below). |
| **Admission policy** (friends only, fail closed) and **TunnelManager** | Unit tests, including mutation checks: breaking either fail-closed rule makes tests fail. |
| **Steam P2P connections and Steam API lookup** (`ListenP2P`/`ConnectP2P`, `SteamRuntime`) | Written and cross-compiled for Windows with MinGW. **Never run against Steam.** |
| **Game client changes** (`TransportService`, `OverlayClient`, `SteamTunnelService`) | **Not compiled and not run**: they need the game's headers and Windows. The portable code they call is tested, and `SteamTunnelService` type-checks against a stub of the game's precompiled header. |
| **UI** (`error.service.ts`, strings) | `tsc` and Prettier pass, with the type check shown to fail on a deliberate error. Not run inside the game's browser. |

### End-to-end run (real server, Linux)

`SkyrimTogetherServer` was started and `TrueMPProbe` authenticated against it
from fake Data folders. Player A joined first and held the connection:

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
| 2,000-plugin install, twice | both accepted (~0.25 s each) |

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
between two players, leaving the game, the server and the Tilted libraries
untouched:

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

Why a tunnel and not a new transport: the Tilted libraries holding the network
code (`TiltedConnect`, `TiltedReverse`, `TiltedHooks`, `TiltedUI`) are "All
Rights Reserved" with no licence to modify (see `NOTICE.md`). The tunnel is new
GPL code beside them.

Risks that only a real test will settle:

* Steam's P2P connection needs either a direct path or Steam's relay. Whether
  Skyrim's AppID (489830) has relay enabled is not something I could check; without
  it, players behind strict NATs may not be able to connect.
* The lookup of Steam's interfaces from the game's own `steam_api64.dll` relies on
  exports that I could not inspect here. If they are missing the code reports
  "Steam unavailable" or falls back to the allow-list; it should not crash, but it
  has not been exercised.

## Building and testing

The server and all tests build on Linux:

```
xmake build TPTests SkyrimTogetherServer SkyrimServerRunner TrueMPProbe
xmake run TPTests
```

(The OpenSSL package xmake wants is a 1.1.1w source download from GitHub; if your
network blocks GitHub archive downloads, build with the system OpenSSL instead.)

`TrueMPProbe` authenticates against a running server from a plain folder of
plugin files, optionally through the tunnel, so the mod policy can be exercised
without Skyrim:

```
truemp_probe --connect 127.0.0.1:10578 --data <dir containing Data/> \
             --plugins Skyrim.esm,MyMod.esp [--tunnel] [--hold SECONDS]
```

The Skyrim client (`Code/client`) is Windows only and needs the game.

## What to test next, on Windows with two Steam accounts

1. The client builds (`Code/client` has three changed or new files).
2. Join your own server on `127.0.0.1`: the log should say
   `Steam friends can join this server with: steam:<id>`.
3. A Steam friend on another machine enters `steam:<id>` and connects.
4. A Steam account that is *not* a friend is refused.
5. A friend with a different mod version sees the "different version" error
   naming the mod.
6. Time the first connect on a realistic mod list (the hashing pause).
