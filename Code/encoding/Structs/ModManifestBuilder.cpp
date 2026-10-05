#include "ModManifestBuilder.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace TrueMP
{
namespace
{
struct CachedHash
{
    uint64_t Size{};
    fs::file_time_type::rep WriteTime{};
    Sha256Digest Digest{};
};

std::mutex g_cacheMutex;
std::unordered_map<std::string, CachedHash> g_cache;

bool HasExtension(const fs::path& acPath, const char* apExtension)
{
    std::string extension = acPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == apExtension;
}

// Cached wrapper around ModManifestBuilder::HashFile.
bool HashFileCached(const fs::path& acPath, uint64_t& aSize, Sha256Digest& aDigest)
{
    std::error_code ec;
    const uint64_t size = fs::file_size(acPath, ec);
    if (ec)
        return false;
    const auto writeTime = fs::last_write_time(acPath, ec).time_since_epoch().count();
    if (ec)
        return false;

    const std::string key = acPath.string();
    {
        std::lock_guard lock(g_cacheMutex);
        const auto it = g_cache.find(key);
        if (it != g_cache.end() && it->second.Size == size && it->second.WriteTime == writeTime)
        {
            aSize = it->second.Size;
            aDigest = it->second.Digest;
            return true;
        }
    }

    if (!ModManifestBuilder::HashFile(acPath, aSize, aDigest))
        return false;

    std::lock_guard lock(g_cacheMutex);
    g_cache[key] = CachedHash{aSize, writeTime, aDigest};
    return true;
}
} // namespace

ModManifestBuilder::ModManifestBuilder(fs::path aDataDirectory)
    : m_dataDirectory(std::move(aDataDirectory))
{
}

bool ModManifestBuilder::HashFile(const fs::path& acPath, uint64_t& aSize, Sha256Digest& aDigest)
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return false;

    Sha256 hasher;
    std::vector<char> chunk(1 << 20);
    uint64_t total = 0;

    while (file)
    {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = file.gcount();
        if (got > 0)
        {
            hasher.Update(chunk.data(), static_cast<size_t>(got));
            total += static_cast<uint64_t>(got);
        }
    }

    // Anything other than a clean end of file means we did not hash the whole thing.
    if (!file.eof())
        return false;

    aSize = total;
    aDigest = hasher.Final();
    return true;
}

bool ModManifestBuilder::AddPlugin(const String& acFilename)
{
    ModManifest::Entry entry;
    entry.Type = ModManifest::Kind::kPlugin;
    entry.Name = acFilename;

    if (!HashFileCached(m_dataDirectory / std::string(acFilename.c_str()), entry.Size, entry.Digest))
    {
        m_failures.push_back(acFilename);
        return false;
    }

    m_manifest.Entries.push_back(std::move(entry));
    return true;
}

void ModManifestBuilder::AddSksePluginsAndArchives()
{
    std::error_code ec;

    // Sorted so two players with the same files always produce the same manifest bytes.
    Vector<fs::path> skseDlls;
    for (fs::directory_iterator it(m_dataDirectory / "SKSE" / "Plugins", ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->is_regular_file(ec) && HasExtension(it->path(), ".dll"))
            skseDlls.push_back(it->path());
    }
    std::sort(skseDlls.begin(), skseDlls.end());

    for (const auto& path : skseDlls)
    {
        ModManifest::Entry entry;
        entry.Type = ModManifest::Kind::kSksePlugin;
        entry.Name = path.filename().string().c_str();

        if (HashFileCached(path, entry.Size, entry.Digest))
            m_manifest.Entries.push_back(std::move(entry));
        else
            m_failures.push_back(entry.Name);
    }

    ec.clear();
    Vector<fs::path> archives;
    for (fs::directory_iterator it(m_dataDirectory, ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->is_regular_file(ec) && HasExtension(it->path(), ".bsa"))
            archives.push_back(it->path());
    }
    std::sort(archives.begin(), archives.end());

    for (const auto& path : archives)
    {
        ec.clear();
        const uint64_t size = fs::file_size(path, ec);
        if (ec)
            continue;

        ModManifest::Entry entry;
        entry.Type = ModManifest::Kind::kArchive;
        entry.Name = path.filename().string().c_str();
        entry.Size = size;
        m_manifest.Entries.push_back(std::move(entry));
    }
}
} // namespace TrueMP
