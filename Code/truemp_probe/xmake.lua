-- SkyrimTrueMP: a tiny protocol client for testing the server end to end without the game.
target("TrueMPProbe")
    set_kind("binary")
    set_group("Tools")
    add_files("main.cpp")
    add_deps("SkyrimEncoding", "SteamTunnel", "TiltedConnect", "CommonLib", "BaseLib")
    add_packages(
        "gamenetworkingsockets",
        "spdlog",
        "hopscotch-map",
        "glm",
        "entt",
        "tiltedcore",
        "snappy",
        "libuv")
