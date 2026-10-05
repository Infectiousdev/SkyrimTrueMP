#pragma once

#include "Message.h"
#include <Structs/Mods.h>
#include <Structs/ModManifest.h>
#include <TiltedCore/Buffer.hpp>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

struct AuthenticationRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kAuthenticationRequest;

    AuthenticationRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~AuthenticationRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AuthenticationRequest& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && DiscordId == achRhs.DiscordId && SKSEActive == achRhs.SKSEActive && MO2Active == achRhs.MO2Active && Token == achRhs.Token && Version == achRhs.Version && UserMods == achRhs.UserMods && Manifest == achRhs.Manifest && Username == achRhs.Username &&
               WorldSpaceId == achRhs.WorldSpaceId && CellId == achRhs.CellId && Level == achRhs.Level
            && PlayerTime == achRhs.PlayerTime;
    }

    uint64_t DiscordId{};
    bool SKSEActive{};
    bool MO2Active{};
    String Token{};
    String Version{};
    Mods UserMods{};
    // SkyrimTrueMP: content fingerprint of the installed mods, see ModManifest.h
    ModManifest Manifest{};
    // False when the manifest in a received request was malformed or over the size limits.
    bool ManifestValid{true};
    String Username{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint16_t Level{};
    TimeModel PlayerTime{};
};
