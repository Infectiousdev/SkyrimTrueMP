-- SkyrimTrueMP network library: client and server over GameNetworkingSockets.
-- Original code (GPLv3-or-later). Consumers include only the headers in this directory; the
-- Valve headers stay private to the library.
target("TrueMPNet")
    set_kind("static")
    set_group("common")
    add_includedirs(".", {public = true})
    add_headerfiles("*.h")
    add_files("*.cpp")
    add_packages("gamenetworkingsockets", "snappy")
    add_defines("STEAMNETWORKINGSOCKETS_STATIC_LINK")

    if is_plat("windows") then
        add_syslinks("ws2_32", "winmm")
    end

    if is_plat("linux") then
        add_cxxflags("-fPIC")
    end
