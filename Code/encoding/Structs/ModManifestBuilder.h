#pragma once

// SkyrimTrueMP: builds a ModManifest from the files in the game's Data directory.
//
// Plain std::filesystem, no game headers, so it is unit tested on any platform.
// Hashing a plugin is the expensive part (Skyrim.esm is ~250 MB), so digests are
// cached per process, keyed by path + size + last write time.

#include "ModManifest.h"

#include <filesystem>

namespace TrueMP
{
class ModManifestBuilder
{
public:
    explicit ModManifestBuilder(std::filesystem::path aDataDirectory);

    // Appends a plugin; call in load order. Returns false if the file could not be read,
    // in which case no entry is added and the name is recorded in Failures().
    bool AddPlugin(const String& acFilename);

    // Appends every Data/SKSE/Plugins/*.dll (hashed) and Data/*.bsa (size only).
    void AddSksePluginsAndArchives();

    [[nodiscard]] const Vector<String>& Failures() const noexcept { return m_failures; }
    [[nodiscard]] ModManifest Build() const { return m_manifest; }

    // Hashes a whole file. Returns false if it cannot be opened or read to the end.
    static bool HashFile(const std::filesystem::path& acPath, uint64_t& aSize, Sha256Digest& aDigest);

private:
    std::filesystem::path m_dataDirectory;
    ModManifest m_manifest;
    Vector<String> m_failures;
};
} // namespace TrueMP
