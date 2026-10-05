// TiltedCore first: the message headers rely on it being included before them, as in encoding.cpp.
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/ModManifest.h>
#include <Structs/ModManifestBuilder.h>
#include <Structs/Sha256.h>

#include <TiltedCore/Buffer.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace TiltedPhoques;
using TrueMP::Sha256;
using TrueMP::Sha256Digest;

namespace
{
std::string Hex(const Sha256Digest& aDigest)
{
    std::string out;
    char byte[3];
    for (uint8_t b : aDigest)
    {
        std::snprintf(byte, sizeof(byte), "%02x", b);
        out += byte;
    }
    return out;
}

// Expected values below were produced with Python's hashlib, not with this implementation.
struct KnownVector
{
    size_t Length;
    const char* Hex;
};

constexpr KnownVector kRepeatedA[] = {
    {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
    {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
    {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
    {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    {121, "e9615320128cc7a3d6078e9af05603188e5ccbf0d07d8b735d3df5e8e0c1281f"},
    {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
    {1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
};

ModManifest::Entry Plugin(const char* aName, uint8_t aFill, uint64_t aSize = 1000)
{
    ModManifest::Entry entry;
    entry.Type = ModManifest::Kind::kPlugin;
    entry.Name = aName;
    entry.Size = aSize;
    entry.Digest.fill(aFill);
    return entry;
}

ModManifest::Entry Other(ModManifest::Kind aKind, const char* aName, uint8_t aFill, uint64_t aSize = 50)
{
    ModManifest::Entry entry = Plugin(aName, aFill, aSize);
    entry.Type = aKind;
    return entry;
}

ModManifest Baseline()
{
    ModManifest manifest;
    manifest.Entries = {Plugin("Skyrim.esm", 1), Plugin("Update.esm", 2), Plugin("MyMod.esp", 3), Other(ModManifest::Kind::kSksePlugin, "SkyUI.dll", 4),
                        Other(ModManifest::Kind::kArchive, "MyMod.bsa", 0, 9000)};
    return manifest;
}

bool Has(const Vector<ModManifest::Issue>& aIssues, ModManifest::Issue::Reason aWhy, const char* aName)
{
    for (const auto& issue : aIssues)
    {
        if (issue.Why == aWhy && issue.Name == aName)
            return true;
    }
    return false;
}
} // namespace

TEST_CASE("Sha256 matches independently computed digests", "[manifest.sha256]")
{
    SECTION("NIST short vectors")
    {
        REQUIRE(Hex(Sha256::Hash("abc", 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

        const std::string nist448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        REQUIRE(Hex(Sha256::Hash(nist448.data(), nist448.size())) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }

    SECTION("padding boundaries and a long input")
    {
        for (const auto& vector : kRepeatedA)
        {
            const std::string input(vector.Length, 'a');
            INFO("length " << vector.Length);
            REQUIRE(Hex(Sha256::Hash(input.data(), input.size())) == vector.Hex);
        }
    }

    SECTION("streaming in odd chunk sizes gives the same digest")
    {
        const std::string input(1000, 'a');
        for (size_t chunk : {size_t(1), size_t(3), size_t(7), size_t(63), size_t(64), size_t(65), size_t(999)})
        {
            Sha256 hasher;
            for (size_t offset = 0; offset < input.size(); offset += chunk)
                hasher.Update(input.data() + offset, std::min(chunk, input.size() - offset));

            INFO("chunk " << chunk);
            REQUIRE(Hex(hasher.Final()) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
        }
    }
}

TEST_CASE("ModManifest compare", "[manifest.compare]")
{
    const ModManifest reference = Baseline();

    SECTION("identical manifests have no issues")
    {
        REQUIRE(ModManifest::Compare(reference, Baseline()).empty());
    }

    SECTION("a missing plugin is reported once, not as a cascade of reorders")
    {
        ModManifest candidate = Baseline();
        candidate.Entries.erase(candidate.Entries.begin() + 1); // drop Update.esm

        const auto issues = ModManifest::Compare(reference, candidate);
        REQUIRE(issues.size() == 1);
        REQUIRE(Has(issues, ModManifest::Issue::Reason::kMissing, "Update.esm"));
    }

    SECTION("an extra plugin is reported")
    {
        ModManifest candidate = Baseline();
        candidate.Entries.insert(candidate.Entries.begin() + 3, Plugin("Extra.esp", 9));

        const auto issues = ModManifest::Compare(reference, candidate);
        REQUIRE(issues.size() == 1);
        REQUIRE(Has(issues, ModManifest::Issue::Reason::kExtra, "Extra.esp"));
    }

    SECTION("same file name with different content is caught")
    {
        ModManifest candidate = Baseline();
        candidate.Entries[2].Digest.fill(0x7f);

        const auto issues = ModManifest::Compare(reference, candidate);
        REQUIRE(issues.size() == 1);
        REQUIRE(Has(issues, ModManifest::Issue::Reason::kContentDiffers, "MyMod.esp"));
    }

    SECTION("same hash but different size is still a difference")
    {
        ModManifest candidate = Baseline();
        candidate.Entries[2].Size = 1001;

        REQUIRE(Has(ModManifest::Compare(reference, candidate), ModManifest::Issue::Reason::kContentDiffers, "MyMod.esp"));
    }

    SECTION("load order differences are caught")
    {
        ModManifest candidate = Baseline();
        std::swap(candidate.Entries[1], candidate.Entries[2]); // Update.esm <-> MyMod.esp

        const auto issues = ModManifest::Compare(reference, candidate);
        REQUIRE_FALSE(issues.empty());
        for (const auto& issue : issues)
            REQUIRE(issue.Why == ModManifest::Issue::Reason::kOutOfOrder);
    }

    SECTION("order of SKSE plugins and archives does not matter")
    {
        ModManifest candidate = Baseline();
        std::swap(candidate.Entries[3], candidate.Entries[4]);

        REQUIRE(ModManifest::Compare(reference, candidate).empty());
    }

    SECTION("names compare case-insensitively")
    {
        ModManifest candidate = Baseline();
        candidate.Entries[2].Name = "MYMOD.ESP";

        REQUIRE(ModManifest::Compare(reference, candidate).empty());
    }

    SECTION("archives are compared by size only")
    {
        ModManifest candidate = Baseline();
        candidate.Entries[4].Size = 9001;

        const auto issues = ModManifest::Compare(reference, candidate);
        REQUIRE(issues.size() == 1);
        REQUIRE(issues[0].Type == ModManifest::Kind::kArchive);
        REQUIRE(issues[0].Why == ModManifest::Issue::Reason::kContentDiffers);
    }

    SECTION("a player with no manifest is missing everything")
    {
        const auto issues = ModManifest::Compare(reference, ModManifest{});
        REQUIRE(issues.size() == reference.Entries.size());
        for (const auto& issue : issues)
            REQUIRE(issue.Why == ModManifest::Issue::Reason::kMissing);
    }

    SECTION("the issue list is capped")
    {
        ModManifest big;
        for (int i = 0; i < 500; ++i)
            big.Entries.push_back(Plugin(("Mod" + std::to_string(i) + ".esp").c_str(), 1));

        REQUIRE(ModManifest::Compare(big, ModManifest{}).size() == ModManifest::kMaxIssues);
    }
}

TEST_CASE("ModManifest serialization", "[manifest.serialize]")
{
    SECTION("round trip")
    {
        const ModManifest original = Baseline();

        Buffer buffer(1 << 14);
        Buffer::Writer writer(&buffer);
        original.Serialize(writer);

        Buffer::Reader reader(&buffer);
        ModManifest decoded;
        REQUIRE(decoded.Deserialize(reader));
        REQUIRE(decoded == original);
    }

    SECTION("a realistic 2000 plugin list fits in a message")
    {
        ModManifest large;
        for (int i = 0; i < 2000; ++i)
            large.Entries.push_back(Plugin(("Some Reasonably Long Plugin Name " + std::to_string(i) + ".esp").c_str(), static_cast<uint8_t>(i % 255 + 1), 12345678));

        Buffer buffer(1 << 20);
        Buffer::Writer writer(&buffer);
        large.Serialize(writer);

        // Documented budget: the client send buffer is 1 MiB and GNS caps messages at 512 KiB.
        REQUIRE(writer.GetBytePosition() < (400 * 1024));

        Buffer::Reader reader(&buffer);
        ModManifest decoded;
        REQUIRE(decoded.Deserialize(reader));
        REQUIRE(decoded == large);
    }

    SECTION("an entry count above the limit is rejected")
    {
        Buffer buffer(64);
        Buffer::Writer writer(&buffer);
        Serialization::WriteVarInt(writer, ModManifest::kMaxEntries + 1);

        Buffer::Reader reader(&buffer);
        ModManifest decoded;
        REQUIRE_FALSE(decoded.Deserialize(reader));
    }

    SECTION("an unknown entry kind is rejected")
    {
        Buffer buffer(256);
        Buffer::Writer writer(&buffer);
        Serialization::WriteVarInt(writer, 1);
        Serialization::WriteVarInt(writer, 77); // no such kind
        Serialization::WriteString(writer, "x.esp");
        Serialization::WriteVarInt(writer, 10);

        Buffer::Reader reader(&buffer);
        ModManifest decoded;
        REQUIRE_FALSE(decoded.Deserialize(reader));
    }

    SECTION("an oversized name is rejected")
    {
        Buffer buffer(1024);
        Buffer::Writer writer(&buffer);
        Serialization::WriteVarInt(writer, 1);
        Serialization::WriteVarInt(writer, 0);
        Serialization::WriteString(writer, String(ModManifest::kMaxNameLength + 1, 'x'));
        Serialization::WriteVarInt(writer, 10);

        Buffer::Reader reader(&buffer);
        ModManifest decoded;
        REQUIRE_FALSE(decoded.Deserialize(reader));
    }
}

TEST_CASE("Manifest travels through the authentication messages", "[manifest.messages]")
{
    SECTION("request")
    {
        AuthenticationRequest request;
        request.Token = "secret";
        request.Manifest = Baseline();

        Buffer buffer(1 << 14);
        Buffer::Writer writer(&buffer);
        request.Serialize(writer);

        Buffer::Reader reader(&buffer);
        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);
        REQUIRE(pMessage);

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->ManifestValid);
        REQUIRE(pRequest->Token == "secret");
        REQUIRE(pRequest->Manifest == request.Manifest);
    }

    SECTION("response carries the issues")
    {
        AuthenticationResponse response;
        response.Type = AuthenticationResponse::ResponseType::kModsMismatch;
        response.ManifestIssues.push_back({ModManifest::Kind::kPlugin, ModManifest::Issue::Reason::kContentDiffers, "MyMod.esp"});
        response.ManifestIssues.push_back({ModManifest::Kind::kSksePlugin, ModManifest::Issue::Reason::kMissing, "SkyUI.dll"});

        Buffer buffer(1 << 12);
        Buffer::Writer writer(&buffer);
        response.Serialize(writer);

        Buffer::Reader reader(&buffer);
        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);
        REQUIRE(pMessage);

        auto pResponse = CastUnique<AuthenticationResponse>(std::move(pMessage));
        REQUIRE(pResponse->Type == AuthenticationResponse::ResponseType::kModsMismatch);
        REQUIRE(pResponse->ManifestIssues == response.ManifestIssues);
    }
}

namespace
{
// A throwaway Data directory on disk.
struct TempData
{
    std::filesystem::path Root;

    TempData()
    {
        Root = std::filesystem::temp_directory_path() / ("truemp_manifest_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::remove_all(Root);
        std::filesystem::create_directories(Root / "SKSE" / "Plugins");
    }
    ~TempData() { std::filesystem::remove_all(Root); }

    void Write(const std::string& aRelative, const std::string& aContent) const
    {
        std::ofstream(Root / aRelative, std::ios::binary) << aContent;
    }
};
} // namespace

TEST_CASE("ModManifestBuilder reads real files", "[manifest.builder]")
{
    TempData data;
    data.Write("Skyrim.esm", "TES4-skyrim");
    data.Write("MyMod.esp", "TES4-mymod");
    data.Write("SKSE/Plugins/SkyUI.dll", "dll-bytes");
    data.Write("SKSE/Plugins/readme.txt", "not a dll");
    data.Write("MyMod.bsa", "archive-bytes");
    data.Write("notes.txt", "ignored");

    TrueMP::ModManifestBuilder builder(data.Root);
    REQUIRE(builder.AddPlugin("Skyrim.esm"));
    REQUIRE(builder.AddPlugin("MyMod.esp"));
    builder.AddSksePluginsAndArchives();
    REQUIRE(builder.Failures().empty());

    const ModManifest manifest = builder.Build();
    REQUIRE(manifest.Entries.size() == 4);

    SECTION("plugins keep the order they were added in and carry real digests")
    {
        REQUIRE(manifest.Entries[0].Name == "Skyrim.esm");
        REQUIRE(manifest.Entries[1].Name == "MyMod.esp");
        REQUIRE(manifest.Entries[0].Size == 11);
        REQUIRE(Hex(manifest.Entries[0].Digest) == Hex(Sha256::Hash("TES4-skyrim", 11)));
    }

    SECTION("SKSE dlls are hashed, archives are size only, other files are ignored")
    {
        REQUIRE(manifest.Entries[2].Type == ModManifest::Kind::kSksePlugin);
        REQUIRE(manifest.Entries[2].Name == "SkyUI.dll");
        REQUIRE(manifest.Entries[2].HasDigest());

        REQUIRE(manifest.Entries[3].Type == ModManifest::Kind::kArchive);
        REQUIRE(manifest.Entries[3].Name == "MyMod.bsa");
        REQUIRE(manifest.Entries[3].Size == 13);
        REQUIRE_FALSE(manifest.Entries[3].HasDigest());
    }

    SECTION("an identical install produces an identical manifest")
    {
        TempData other;
        other.Write("Skyrim.esm", "TES4-skyrim");
        other.Write("MyMod.esp", "TES4-mymod");
        other.Write("SKSE/Plugins/SkyUI.dll", "dll-bytes");
        other.Write("MyMod.bsa", "archive-bytes");

        TrueMP::ModManifestBuilder otherBuilder(other.Root);
        otherBuilder.AddPlugin("Skyrim.esm");
        otherBuilder.AddPlugin("MyMod.esp");
        otherBuilder.AddSksePluginsAndArchives();

        REQUIRE(ModManifest::Compare(manifest, otherBuilder.Build()).empty());
    }

    SECTION("a one byte change in a plugin is detected")
    {
        TempData other;
        other.Write("Skyrim.esm", "TES4-skyrim");
        other.Write("MyMod.esp", "TES4-mymoD"); // same name and size, last byte differs
        other.Write("SKSE/Plugins/SkyUI.dll", "dll-bytes");
        other.Write("MyMod.bsa", "archive-bytes");

        TrueMP::ModManifestBuilder otherBuilder(other.Root);
        otherBuilder.AddPlugin("Skyrim.esm");
        otherBuilder.AddPlugin("MyMod.esp");
        otherBuilder.AddSksePluginsAndArchives();

        const auto issues = ModManifest::Compare(manifest, otherBuilder.Build());
        REQUIRE(issues.size() == 1);
        REQUIRE(Has(issues, ModManifest::Issue::Reason::kContentDiffers, "MyMod.esp"));
    }

    SECTION("a plugin that cannot be read is reported, never silently skipped")
    {
        TrueMP::ModManifestBuilder missing(data.Root);
        REQUIRE_FALSE(missing.AddPlugin("DoesNotExist.esp"));
        REQUIRE(missing.Failures().size() == 1);
        REQUIRE(missing.Build().Entries.empty());
    }
}

TEST_CASE("A plugin without a digest is not a valid manifest", "[manifest.serialize]")
{
    ModManifest manifest;
    manifest.Entries.push_back(Plugin("Unreadable.esp", 0)); // zero digest

    Buffer buffer(1 << 10);
    Buffer::Writer writer(&buffer);
    manifest.Serialize(writer);

    Buffer::Reader reader(&buffer);
    ModManifest decoded;
    REQUIRE_FALSE(decoded.Deserialize(reader));
}
