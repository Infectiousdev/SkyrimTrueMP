-- SkyrimTrueMP: Steam-friend tunnel. The core (Tunnel, UdpSocket, LoopbackLink) is portable and
-- unit tested everywhere; the Steam relay link only exists on Windows, where the game runs.
target("SteamTunnel")
    set_kind("static")
    set_group("common")
    add_includedirs(".", {public = true})
    add_headerfiles("*.h")
    add_files("Tunnel.cpp", "UdpSocket.cpp", "LoopbackLink.cpp", "GnsLink.cpp")
    add_packages("gamenetworkingsockets", {public = true})
    add_defines("STEAMNETWORKINGSOCKETS_STATIC_LINK")

    if is_plat("linux") then
        add_cxxflags("-fPIC")
    end

    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
