// truemp_probe: authenticates against a SkyrimTrueMP server the way the game client does,
// but from a plain Data folder and plugin list, so the mod policy (and the Steam tunnel)
// can be tested end to end without Skyrim.
//
//   truemp_probe --connect 127.0.0.1:10578 --data <dir> --plugins A.esm,B.esp
//                [--password P] [--name N] [--hold SECONDS] [--tunnel] [--timeout SECONDS]
//
// Exit code: 0 accepted, 10 refused for mods, 11 refused for another reason, 12 no answer.

// Include order matters: the message headers expect TiltedCore to come first.
#include <chrono>

#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Stl.hpp>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <BuildInfo.h>
#include <Client.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/ModManifestBuilder.h>

#include <LoopbackLink.h>
#include <Tunnel.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

using namespace std::chrono;

namespace
{
struct Options
{
    std::string Connect = "127.0.0.1:10578";
    std::string Data;
    std::string Plugins;
    std::string Password;
    std::string Name = "Probe";
    int HoldSeconds = 0;
    int TimeoutSeconds = 15;
    bool Tunnel = false;
};

bool ParseOptions(int argc, char** argv, Options& aOptions)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };

        if (arg == "--connect") aOptions.Connect = next();
        else if (arg == "--data") aOptions.Data = next();
        else if (arg == "--plugins") aOptions.Plugins = next();
        else if (arg == "--password") aOptions.Password = next();
        else if (arg == "--name") aOptions.Name = next();
        else if (arg == "--hold") aOptions.HoldSeconds = std::atoi(next().c_str());
        else if (arg == "--timeout") aOptions.TimeoutSeconds = std::atoi(next().c_str());
        else if (arg == "--tunnel") aOptions.Tunnel = true;
        else
        {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return false;
        }
    }
    return !aOptions.Data.empty();
}

const char* ReasonText(ModManifest::Issue::Reason aReason)
{
    switch (aReason)
    {
    case ModManifest::Issue::Reason::kMissing: return "missing";
    case ModManifest::Issue::Reason::kExtra: return "extra";
    case ModManifest::Issue::Reason::kContentDiffers: return "differs";
    case ModManifest::Issue::Reason::kOutOfOrder: return "order";
    }
    return "?";
}

const char* KindText(ModManifest::Kind aKind)
{
    switch (aKind)
    {
    case ModManifest::Kind::kPlugin: return "plugin";
    case ModManifest::Kind::kSksePlugin: return "skse";
    case ModManifest::Kind::kArchive: return "archive";
    }
    return "?";
}

struct Probe final : TrueMP::Net::Client
{
    explicit Probe(Options aOptions)
        : Options_(std::move(aOptions))
    {
    }

    void OnConnected() override
    {
        std::stringstream plugins(Options_.Plugins);
        std::string plugin;

        TrueMP::ModManifestBuilder builder(std::filesystem::path(Options_.Data) / "Data");
        AuthenticationRequest request;
        request.Version = BUILD_COMMIT;
        request.Token = Options_.Password.c_str();
        request.Username = Options_.Name.c_str();
        request.Level = 1;

        uint16_t id = 0;
        while (std::getline(plugins, plugin, ','))
        {
            if (plugin.empty())
                continue;

            Mods::Entry entry;
            entry.Id = id++;
            entry.IsLite = false;
            entry.Filename = plugin.c_str();
            request.UserMods.ModList.push_back(entry);
            builder.AddPlugin(entry.Filename);
        }
        builder.AddSksePluginsAndArchives();
        for (const auto& failure : builder.Failures())
            std::fprintf(stderr, "probe: could not read %s\n", failure.c_str());
        request.Manifest = builder.Build();

        thread_local TiltedPhoques::ScratchAllocator s_allocator(1 << 22);
        TiltedPhoques::ScopedAllocator scoped{s_allocator};
        TiltedPhoques::Buffer buffer(1 << 20);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        request.Serialize(writer);
        Send(buffer.GetData(), writer.Size());
        s_allocator.Reset();
    }

    void OnConsume(const void* apData, uint32_t aSize) override
    {
        ServerMessageFactory factory;
        TiltedPhoques::ViewBuffer view(reinterpret_cast<uint8_t*>(const_cast<void*>(apData)), aSize);
        TiltedPhoques::Buffer::Reader reader(&view);

        auto pMessage = factory.Extract(reader);
        if (!pMessage || pMessage->GetOpcode() != AuthenticationResponse::Opcode || Response)
            return;

        Response = *TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(pMessage));
    }

    void OnDisconnected(EDisconnectReason aReason) override { Disconnected = true; }
    void OnUpdate() override {}

    Options Options_;
    std::optional<AuthenticationResponse> Response;
    bool Disconnected = false;
};
} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::fprintf(stderr, "usage: truemp_probe --data <dir> [--connect host:port] [--plugins a.esm,b.esp] [--password p] [--name n] [--hold s] [--tunnel] [--timeout s]\n");
        return 2;
    }

    Probe probe(options);

    // Optionally route the connection through the Steam tunnel core with an in-process link,
    // standing in for the Steam relay: probe --UDP--> TunnelClient ==link==> TunnelHost --UDP--> server.
    std::unique_ptr<TrueMP::Steam::LoopbackNetwork> network;
    std::unique_ptr<TrueMP::Steam::IPeerLink> clientLink;
    std::unique_ptr<TrueMP::Steam::TunnelHost> tunnelHost;
    std::unique_ptr<TrueMP::Steam::TunnelClient> tunnelClient;

    std::string endpoint = options.Connect;
    if (options.Tunnel)
    {
        const auto colon = options.Connect.find_last_of(':');
        const uint16_t serverPort = static_cast<uint16_t>(std::atoi(options.Connect.c_str() + colon + 1));

        network = std::make_unique<TrueMP::Steam::LoopbackNetwork>();
        tunnelHost = std::make_unique<TrueMP::Steam::TunnelHost>(network->Host(), TrueMP::Steam::TunnelHost::Config{serverPort, 8}, [](TrueMP::Steam::PeerId) { return true; });
        clientLink = network->Connect(4242);
        tunnelClient = std::make_unique<TrueMP::Steam::TunnelClient>(*clientLink, TrueMP::Steam::LoopbackNetwork::kHostId);
        if (!tunnelClient->Start())
        {
            std::fprintf(stderr, "probe: could not open the tunnel's local port\n");
            return 12;
        }
        endpoint = "127.0.0.1:" + std::to_string(tunnelClient->LocalPort());
        std::printf("TUNNEL local port %u -> server port %u\n", tunnelClient->LocalPort(), serverPort);
    }

    const auto pump = [&]
    {
        if (tunnelHost)
        {
            tunnelHost->Pump();
            tunnelClient->Pump();
        }
        probe.Update();
    };

    if (tunnelClient)
    {
        // The game would connect only once the link is up.
        const auto linkDeadline = steady_clock::now() + seconds(5);
        while (!tunnelClient->IsLinkUp() && steady_clock::now() < linkDeadline)
        {
            pump();
            std::this_thread::sleep_for(milliseconds(1));
        }
    }

    if (!probe.Connect(endpoint))
    {
        std::fprintf(stderr, "probe: could not start connecting to %s\n", endpoint.c_str());
        return 12;
    }

    const auto deadline = steady_clock::now() + seconds(options.TimeoutSeconds);
    while (!probe.Response && !probe.Disconnected && steady_clock::now() < deadline)
    {
        pump();
        std::this_thread::sleep_for(milliseconds(1));
    }

    if (!probe.Response)
    {
        std::printf("RESULT no-answer%s\n", probe.Disconnected ? " (disconnected)" : "");
        return 12;
    }

    const auto& response = *probe.Response;
    using RT = AuthenticationResponse::ResponseType;
    if (response.Type == RT::kAccepted)
    {
        std::printf("RESULT accepted\n");
        const auto holdUntil = steady_clock::now() + seconds(options.HoldSeconds);
        while (steady_clock::now() < holdUntil && !probe.Disconnected)
        {
            pump();
            std::this_thread::sleep_for(milliseconds(5));
        }
        probe.Close();
        return 0;
    }

    std::printf("RESULT refused type=%d issues=%zu\n", static_cast<int>(response.Type), response.ManifestIssues.size());
    for (const auto& issue : response.ManifestIssues)
        std::printf("ISSUE %s %s %s\n", ReasonText(issue.Why), KindText(issue.Type), issue.Name.c_str());
    probe.Close();
    return response.Type == RT::kModsMismatch ? 10 : 11;
}
