# SkyrimTrueMP: modifications to Tilted Evolution

GPLv3 section 5(a) notice: this file lists the files this fork changed relative to
upstream, and when. Base: `tiltedphoques/TiltedEvolution` `dev` @ `4917189f`
(2026-10-03). Anything not listed here is unmodified upstream.

## Added files (all 2026-10-05)

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

## Modified upstream files (all 2026-10-05)

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
