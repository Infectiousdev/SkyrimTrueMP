#include <catch2/catch.hpp>

#include <SteamRuntime.h>

#include <filesystem>
#include <fstream>

using namespace TrueMP::Steam;

TEST_CASE("ParseSteamAddress", "[steam.address]")
{
    REQUIRE(ParseSteamAddress("steam:76561198000000001") == 76561198000000001ull);
    REQUIRE(ParseSteamAddress("steam://76561198000000001") == 76561198000000001ull);

    // Not Steam addresses, or not valid ones.
    REQUIRE(ParseSteamAddress("127.0.0.1") == 0);
    REQUIRE(ParseSteamAddress("localhost") == 0);
    REQUIRE(ParseSteamAddress("steam:") == 0);
    REQUIRE(ParseSteamAddress("steam:abc") == 0);
    REQUIRE(ParseSteamAddress("steam:123abc") == 0);
    REQUIRE(ParseSteamAddress("steam:-5") == 0);
    REQUIRE(ParseSteamAddress("steam:99999999999999999999999") == 0); // does not fit in 64 bits
    REQUIRE(ParseSteamAddress("Steam:76561198000000001") == 0);       // prefix is case sensitive
}

TEST_CASE("LoadAllowList", "[steam.allowlist]")
{
    const auto path = std::filesystem::temp_directory_path() / "truemp_allowlist_test.txt";

    SECTION("reads ids, ignores comments, blanks and junk, and handles Windows line endings")
    {
        std::ofstream(path, std::ios::binary) << "# my friends\r\n"
                                                 "76561198000000001\r\n"
                                                 "\r\n"
                                                 "  76561198000000002  # trailing comment\r\n"
                                                 "not-a-number\r\n"
                                                 "0\r\n"
                                                 "99999999999999999999999\r\n"
                                                 "76561198000000001\r\n"; // duplicate
        const auto ids = LoadAllowList(path);
        REQUIRE(ids == std::set<PeerId>{76561198000000001ull, 76561198000000002ull});
    }

    SECTION("a missing file is an empty list")
    {
        std::filesystem::remove(path);
        REQUIRE(LoadAllowList(path).empty());
    }

    std::filesystem::remove(path);
}

TEST_CASE("Off Windows, Steam is reported unavailable rather than crashing", "[steam.runtime]")
{
#ifndef _WIN32
    std::string error;
    REQUIRE_FALSE(AcquireSteamEnvironment(error, {}));
    REQUIRE_FALSE(error.empty());
#else
    SUCCEED("exercised only inside the game");
#endif
}
