# SkyrimTrueMP: modifications to Tilted Evolution

GPLv3 section 5(a) notice: this file lists the files this fork changed relative to
upstream, and when. Base: `tiltedphoques/TiltedEvolution` `dev` @ `4917189f`
(2026-10-03). Anything not listed here is unmodified upstream.

## Added files

### 2026-10-05

| File | Purpose |
|---|---|
| `NOTICE.md`, `SKYRIMTRUEMP_CHANGES.md`, `docs/SKYRIMTRUEMP.md` | Attribution, this log, design and status |
| `Code/encoding/Structs/Sha256.{h,cpp}` | Dependency-free SHA-256 |
| `Code/encoding/Structs/ModManifest.{h,cpp}` | Mod manifest wire format and comparison |
| `Code/encoding/Structs/ModManifestBuilder.{h,cpp}` | Hashes the Data directory into a manifest |
| `Code/steam_tunnel/` (`Link.h`, `UdpSocket`, `Tunnel`, `LoopbackLink`, `GnsLink`, `TunnelManager`, `SteamRuntime`) | Steam-friend tunnel |
| `Code/client/Services/SteamTunnelService.h`, `Code/client/Services/Generic/SteamTunnelService.cpp` | Client glue for the tunnel |
| `Code/truemp_probe/` | End-to-end protocol client for testing |
| `Code/tests/{mod_manifest,steam_tunnel,gns_link,tunnel_manager,steam_runtime}.cpp`, `Code/tests/gns_runtime.h` | Unit tests |

### 2026-10-05 and later: replacing closed Tilted libraries

| File | Purpose |
|---|---|
| `Code/net/` (`Net.h`, `Frame`, `Clock`, `Resolve`, `Gns`, `Client`, `Server`, `xmake.lua`) | **TrueMPNet**: our own network library, replaces `TiltedConnect` |
| `Code/core/TiltedCore/*.hpp`, `Code/core/README.md` | Our own header-only core library, replaces `TiltedCore` |
| `Code/tests/net_frame.cpp`, `Code/tests/net_link.cpp` | Tests for TrueMPNet (framing, compression, clock, loopback client/server, kick, limits) |
| `Code/tests/core.cpp` | Tests for `Code/core` (30 cases, including the game's CRC-64 key) |

## Modified upstream files

### 2026-10-05

| File | Change |
|---|---|
| `Code/encoding/Messages/AuthenticationRequest.{h,cpp}` | Carries the mod manifest |
| `Code/encoding/Messages/AuthenticationResponse.{h,cpp}` | Carries the list of manifest differences |
| `Code/server/GameServer.{h,cpp}` | `ModPolicy:bEnableManifestCheck`; first player is the reference |
| `Code/client/Services/Generic/TransportService.cpp` | Builds and sends the manifest; 1 MiB send buffer; passes `issues` to the UI |
| `Code/client/Services/Generic/OverlayClient.cpp` | `steam:<id>` addresses; offers a local server to Steam friends |
| `Code/client/xmake.lua` | Links `SteamTunnel` |
| `Code/skyrim_ui/src/app/services/error.service.ts`, `Code/skyrim_ui/src/assets/i18n/en.json` | Explains which mods differ; Steam errors; escapes mod names |
| `Code/xmake.lua`, `Code/tests/xmake.lua` | New targets |
| `xmake.lua` | Crypto++ is required on Windows only |

### 2026-10-05 and later: Windows only, TrueMPNet and `Code/core`

| File | Change |
|---|---|
| `Code/server/Pch.h`, `Code/server/GameServer.{h,cpp}`, `Code/server/Events/{PacketEvent,AdminPacketEvent}.h`, `Code/server/Services/ServerListService.{h,cpp}`, `Code/server/xmake.lua` | Use TrueMPNet's `Server` instead of TiltedConnect's. Messages are serialised straight into the send call (no reserved leading byte); the remote address is read from the server by connection id. `int32` becomes `int32_t` |
| `Code/admin/{AdminApp.h,Transport.cpp,xmake.lua}` | Use TrueMPNet's `Client` |
| `Code/client/Services/TransportService.h`, `Code/client/Services/Generic/{TransportService,OverlayService}.cpp`, `Code/client/xmake.lua` | Use TrueMPNet's `Client`; connection statistics come from `GetConnectionStatus()` |
| `Code/truemp_probe/{main.cpp,xmake.lua}` | Speaks the TrueMPNet protocol |
| `Code/tests/encoding.cpp` | One call wrapped in `TP_UNUSED(...)` |
| `xmake.lua` | `tiltedcore` is a local header-only package built from `Code/core`, and `Code/core` is added as a live include directory; packages TiltedConnect used to declare (`hopscotch-map`, `snappy`, `gamenetworkingsockets`, `catch2`, `libuv`, pinned `protobuf` and `abseil`) are now declared here; `NDEBUG` in release |
| `Libraries/xmake.lua`, `.gitmodules` | `TiltedConnect` removed |
| `NOTICE.md`, `docs/SKYRIMTRUEMP.md` | Updated for the above |
| `.github/workflows/release.yml` | Windows build only |

## Removed files

| File | Why |
|---|---|
| `Libraries/TiltedConnect` (submodule) | Closed licence; replaced by `Code/net` |
| `Dockerfile`, `.dockerignore`, `docker-compose.yml`, `docker-multiarch-build.sh`, `flake.nix`, `flake.lock`, `MakeLinux.cmd`, `.github/workflows/linux.yml`, `.github/workflows/docker-server-image.yml` | This fork is Windows only |
