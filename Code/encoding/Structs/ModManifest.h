#pragma once

// SkyrimTrueMP: mod manifest.
//
// Upstream only compared plugin *file names* against a hand-written
// Data/loadorder.txt on the server. The manifest fingerprints what the player
// actually has installed, so two players can be required to run identical
// mods (same files, same versions, same load order) without anyone having to
// maintain a text file.

#include "Sha256.h"

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>

#include <cstdint>

using TiltedPhoques::String;
using TiltedPhoques::Vector;

struct ModManifest
{
    enum class Kind : uint8_t
    {
        kPlugin = 0,     // .esp/.esm/.esl, listed in load order
        kSksePlugin = 1, // Data/SKSE/Plugins/*.dll
        kArchive = 2     // .bsa/.ba2, compared by name and size only
    };

    struct Entry
    {
        Kind Type{Kind::kPlugin};
        String Name{};
        uint64_t Size{};
        // All zero means "not hashed": only the size is compared.
        TrueMP::Sha256Digest Digest{};

        [[nodiscard]] bool HasDigest() const noexcept;
        bool operator==(const Entry& acRhs) const noexcept;
        bool operator!=(const Entry& acRhs) const noexcept { return !operator==(acRhs); }
    };

    struct Issue
    {
        enum class Reason : uint8_t
        {
            kMissing,        // the reference has it, this player does not
            kExtra,          // this player has it, the reference does not
            kContentDiffers, // same name, different size or hash
            kOutOfOrder      // same plugins, different load order
        };

        Kind Type{Kind::kPlugin};
        Reason Why{Reason::kMissing};
        String Name{};

        bool operator==(const Issue& acRhs) const noexcept { return Type == acRhs.Type && Why == acRhs.Why && Name == acRhs.Name; }
    };

    // Hard limits so a hostile client cannot make the server allocate without bound.
    static constexpr size_t kMaxEntries = 8192;
    static constexpr size_t kMaxNameLength = 260;
    static constexpr size_t kMaxIssues = 64;

    // Plugins appear in load order; other kinds are unordered.
    Vector<Entry> Entries{};

    bool operator==(const ModManifest& acRhs) const noexcept { return Entries == acRhs.Entries; }
    bool operator!=(const ModManifest& acRhs) const noexcept { return !operator==(acRhs); }

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    // Returns false when the data is malformed or exceeds the limits above.
    [[nodiscard]] bool Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    // Differences between a reference manifest and a candidate; empty means identical.
    // Names compare case-insensitively (Windows file system). At most kMaxIssues are returned.
    [[nodiscard]] static Vector<Issue> Compare(const ModManifest& acReference, const ModManifest& acCandidate) noexcept;

    static void SerializeIssues(const Vector<Issue>& acIssues, TiltedPhoques::Buffer::Writer& aWriter) noexcept;
    [[nodiscard]] static bool DeserializeIssues(Vector<Issue>& aIssues, TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
