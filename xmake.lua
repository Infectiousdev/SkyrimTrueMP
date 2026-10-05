set_xmakever("3.0.0")

-- If newer version of xmake, remove ccache until it actually works
if set_policy ~= nil then
    set_policy("build.ccache", false)
end

-- c code will use c99,
set_languages("c99", "cxx20")

if is_plat("windows") then
    add_cxflags("/bigobj")
    add_syslinks("kernel32")
    set_arch("x64")
    set_runtimes("MT")
end

if is_plat("linux") then
    add_cxflags("-fPIC")
end

set_warnings("all")
add_vectorexts("sse", "sse2", "sse3", "ssse3")
add_vectorexts("neon")

-- build configurations
add_rules("mode.debug", "mode.releasedbg", "mode.release")

if has_config("unitybuild") then
    add_rules("c.unity_build")
    add_rules("c++.unity_build", {batchsize = 12})
end

-- The game's core library (containers, buffers, serialization, allocators, ...) is this project's
-- own implementation in Code/core.
--
-- Every target compiles against the live headers through this include directory, so editing one takes
-- effect on the next build. (Directories added with add_includedirs are searched before a package's
-- system include directory, which is what keeps the copy below from ever being preferred.)
add_includedirs(path.join(os.scriptdir(), "Code", "core"))

-- The same headers are also offered as a package named "tiltedcore", because the third-party hook
-- libraries in Libraries/ ask for that name in their own build files. Nothing is downloaded: the
-- package is just a copy of Code/core, and brings the dependencies the headers need.
package("tiltedcore")
    set_kind("library", {headeronly = true})
    set_description("SkyrimTrueMP core library: header-only, original implementation")
    add_deps("mimalloc", "hopscotch-map")
    set_sourcedir(path.join(os.scriptdir(), "Code", "core"))
    set_policy("package.install_always", true)
    on_install(function (package)
        os.cp("TiltedCore", package:installdir("include"))
    end)
package_end()

-- direct dependencies version pinning 
add_requires(
    "entt v3.10.0", 
    "recastnavigation v1.6.0", 
    "tiltedcore", 
    "spdlog v1.13.0", 
    "cpp-httplib 0.14.0",
    "gtest v1.14.0", 
    "mem 1.0.0", 
    "glm 0.9.9+8", 
    "zlib v1.3.1"
)
if is_plat("windows") then
    add_requires(
        "cryptopp 8.9.0", -- SkyrimTrueMP: only the Windows client/launcher use it, so Linux server builds skip it
        "discord 3.2.1", 
        "imgui v1.89.7"
    )
end

-- Packages used by the network library, the tunnel and the tests. They used to be declared by
-- the third-party TiltedConnect build file, which this fork no longer uses.
add_requires(
    "hopscotch-map v2.3.1",
    "snappy 1.1.10",
    "gamenetworkingsockets v1.4.1",
    "catch2 2.13.9",
    "libuv v1.48.0"
)

-- dependencies' dependencies version pinning
add_requireconfs("*.protobuf*", { version = "26.1", override = true })
add_requireconfs("**.abseil*", { version = "20250127.1", override = true })
add_requireconfs("*.mimalloc", { version = "2.2.4", override = true })
add_requireconfs("*.cmake", { version = "3.30.2", override = true })
add_requireconfs("*.openssl", { version = "1.1.1-w", override = true })
add_requireconfs("*.zlib", { version = "v1.3.1", override = true })
if is_plat("linux") then
    add_requireconfs("*.libcurl", { version = "8.7.1", override = true })
end

add_requireconfs("cpp-httplib", {configs = {ssl = true}})
--[[
add_requireconfs("magnum", { configs = { sdl2 = true }})
add_requireconfs("magnum-integration",  { configs = { imgui = true }})
add_requireconfs("magnum-integration.magnum",  { configs = { sdl2 = true }})
add_requireconfs("magnum-integration.imgui", { override = true })
--]]

before_build(function (target)
    import("modules.version")
    local branch, commitHash = version()
    bool_to_number={ [true]=1, [false]=0 }
    local contents = string.format([[
    #pragma once
    #define IS_MASTER %d
    #define IS_BRANCH_BETA %d
    #define IS_BRANCH_PREREL %d
    ]], 
    bool_to_number[branch == "master"], 
    bool_to_number[branch == "bluedove"], 
    bool_to_number[branch == "prerel"])

    -- fix always-compiles problem by updating the file only if content has changed.
    local filepath = "build/BranchInfo.h"
    local old_content = nil
    if os.exists(filepath) then
        old_content = io.readfile(filepath)
    end
    if old_content ~= contents then
        print("Updating file:", filepath)
        io.writefile(filepath, contents)
    end
end)

if is_mode("debug") then
    add_defines("DEBUG")
end

-- (also set by the removed TiltedConnect build file for every target)
if is_mode("release") then
    add_defines("NDEBUG")
end

if is_plat("windows") then
    add_defines("NOMINMAX")
end

-- add projects
includes("Libraries")
includes("Code")
