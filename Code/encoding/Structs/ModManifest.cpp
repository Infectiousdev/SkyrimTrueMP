#include "ModManifest.h"

#include <TiltedCore/Serialization.hpp>

#include <algorithm>
#include <cstring>

using TiltedPhoques::Serialization;

namespace
{
String FoldCase(const String& acName)
{
    String folded = acName;
    for (auto& c : folded)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return folded;
}

// Identity of an entry across two manifests: kind plus case-folded name.
String KeyOf(const ModManifest::Entry& acEntry)
{
    String key(1, static_cast<char>('0' + static_cast<int>(acEntry.Type)));
    key += FoldCase(acEntry.Name);
    return key;
}

bool ContentMatches(const ModManifest::Entry& acRef, const ModManifest::Entry& acCand)
{
    if (acRef.Size != acCand.Size)
        return false;

    // Entries that were not hashed (archives) are compared by size alone.
    if (acRef.HasDigest() && acCand.HasDigest())
        return acRef.Digest == acCand.Digest;

    return acRef.HasDigest() == acCand.HasDigest();
}
} // namespace

bool ModManifest::Entry::HasDigest() const noexcept
{
    return std::any_of(Digest.begin(), Digest.end(), [](uint8_t aByte) { return aByte != 0; });
}

bool ModManifest::Entry::operator==(const Entry& acRhs) const noexcept
{
    return Type == acRhs.Type && Name == acRhs.Name && Size == acRhs.Size && Digest == acRhs.Digest;
}

void ModManifest::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const size_t count = std::min(Entries.size(), kMaxEntries);
    Serialization::WriteVarInt(aWriter, count);

    for (size_t i = 0; i < count; ++i)
    {
        const Entry& entry = Entries[i];
        Serialization::WriteVarInt(aWriter, static_cast<uint64_t>(entry.Type));
        Serialization::WriteString(aWriter, entry.Name);
        Serialization::WriteVarInt(aWriter, entry.Size);
        aWriter.WriteBytes(entry.Digest.data(), entry.Digest.size());
    }
}

bool ModManifest::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Entries.clear();

    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxEntries)
        return false;

    Entries.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i)
    {
        Entry entry;

        const uint64_t type = Serialization::ReadVarInt(aReader);
        if (type > static_cast<uint64_t>(Kind::kArchive))
            return false;
        entry.Type = static_cast<Kind>(type);

        entry.Name = Serialization::ReadString(aReader);
        if (entry.Name.size() > kMaxNameLength)
            return false;

        entry.Size = Serialization::ReadVarInt(aReader);
        if (!aReader.ReadBytes(entry.Digest.data(), entry.Digest.size()))
            return false;

        // Only archives may go unhashed. A plugin or SKSE DLL without a digest means the sender
        // could not read the file, which must not be allowed to look like a match.
        if (entry.Type != Kind::kArchive && !entry.HasDigest())
            return false;

        Entries.push_back(std::move(entry));
    }

    return true;
}

Vector<ModManifest::Issue> ModManifest::Compare(const ModManifest& acReference, const ModManifest& acCandidate) noexcept
{
    Vector<Issue> issues;
    const auto report = [&issues](Kind aType, Issue::Reason aWhy, const String& acName)
    {
        if (issues.size() < kMaxIssues)
            issues.push_back(Issue{aType, aWhy, acName});
    };

    // Index the candidate by identity.
    TiltedPhoques::Map<String, const Entry*> candidateByKey;
    for (const Entry& entry : acCandidate.Entries)
        candidateByKey.emplace(KeyOf(entry), &entry);

    TiltedPhoques::Map<String, const Entry*> referenceByKey;
    for (const Entry& entry : acReference.Entries)
        referenceByKey.emplace(KeyOf(entry), &entry);

    // Missing files and files whose content differs.
    for (const Entry& ref : acReference.Entries)
    {
        const auto it = candidateByKey.find(KeyOf(ref));
        if (it == candidateByKey.end())
            report(ref.Type, Issue::Reason::kMissing, ref.Name);
        else if (!ContentMatches(ref, *it->second))
            report(ref.Type, Issue::Reason::kContentDiffers, ref.Name);
    }

    // Files the reference does not have.
    for (const Entry& cand : acCandidate.Entries)
    {
        if (referenceByKey.find(KeyOf(cand)) == referenceByKey.end())
            report(cand.Type, Issue::Reason::kExtra, cand.Name);
    }

    // Load order: compare the plugin sequences restricted to plugins both sides have,
    // so a single missing plugin does not flag everything after it as out of order.
    Vector<const Entry*> referenceOrder;
    Vector<const Entry*> candidateOrder;
    for (const Entry& ref : acReference.Entries)
    {
        if (ref.Type == Kind::kPlugin && candidateByKey.find(KeyOf(ref)) != candidateByKey.end())
            referenceOrder.push_back(&ref);
    }
    for (const Entry& cand : acCandidate.Entries)
    {
        if (cand.Type == Kind::kPlugin && referenceByKey.find(KeyOf(cand)) != referenceByKey.end())
            candidateOrder.push_back(&cand);
    }

    // Both lists now hold the same set of plugins (barring duplicate names), so a
    // positional walk finds every plugin that sits in a different place.
    const size_t common = std::min(referenceOrder.size(), candidateOrder.size());
    for (size_t i = 0; i < common; ++i)
    {
        if (FoldCase(referenceOrder[i]->Name) != FoldCase(candidateOrder[i]->Name))
            report(Kind::kPlugin, Issue::Reason::kOutOfOrder, candidateOrder[i]->Name);
    }

    return issues;
}

void ModManifest::SerializeIssues(const Vector<Issue>& acIssues, TiltedPhoques::Buffer::Writer& aWriter) noexcept
{
    const size_t count = std::min(acIssues.size(), kMaxIssues);
    Serialization::WriteVarInt(aWriter, count);

    for (size_t i = 0; i < count; ++i)
    {
        Serialization::WriteVarInt(aWriter, static_cast<uint64_t>(acIssues[i].Type));
        Serialization::WriteVarInt(aWriter, static_cast<uint64_t>(acIssues[i].Why));
        Serialization::WriteString(aWriter, acIssues[i].Name);
    }
}

bool ModManifest::DeserializeIssues(Vector<Issue>& aIssues, TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    aIssues.clear();

    const uint64_t count = Serialization::ReadVarInt(aReader);
    if (count > kMaxIssues)
        return false;

    for (uint64_t i = 0; i < count; ++i)
    {
        Issue issue;

        const uint64_t type = Serialization::ReadVarInt(aReader);
        const uint64_t why = Serialization::ReadVarInt(aReader);
        if (type > static_cast<uint64_t>(Kind::kArchive) || why > static_cast<uint64_t>(Issue::Reason::kOutOfOrder))
            return false;

        issue.Type = static_cast<Kind>(type);
        issue.Why = static_cast<Issue::Reason>(why);
        issue.Name = Serialization::ReadString(aReader);
        if (issue.Name.size() > kMaxNameLength)
            return false;

        aIssues.push_back(std::move(issue));
    }

    return true;
}
