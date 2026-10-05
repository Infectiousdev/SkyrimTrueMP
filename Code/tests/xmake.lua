
target("TPTests")
    set_kind("binary")
    set_group("Tests")
    add_includedirs(
        ".", "../encoding", "../steam_tunnel", "../net")
    add_headerfiles("**.h")
    add_files("*.cpp")
    add_deps("SkyrimEncoding", "SteamTunnel", "TrueMPNet")
    add_packages(
        "tiltedcore",
        "hopscotch-map",
        "catch2",
        "mimalloc",
        "gamenetworkingsockets",
        "snappy",
        "glm")
    add_defines("STEAMNETWORKINGSOCKETS_STATIC_LINK")
