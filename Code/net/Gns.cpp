#include "Gns.h"

#include <mutex>
#include <set>

namespace TrueMP::Net::Gns
{
namespace
{
std::mutex g_mutex;
size_t g_users = 0;
bool g_ready = false;
std::set<const StatusSink*> g_sinks;

// Ceiling for the send rate estimate. The library's default is far below what 8 players need.
constexpr int32 kSendRateMax = 4 * 1024 * 1024;

void Trampoline(SteamNetConnectionStatusChangedCallback_t* apChange)
{
    if (!apChange)
        return;

    const auto* pOwner = reinterpret_cast<const StatusSink*>(apChange->m_info.m_nUserData);

    // Held for the whole call so an owner cannot be destroyed while it is being notified.
    std::lock_guard lock(g_mutex);
    if (g_sinks.count(pOwner))
        const_cast<StatusSink*>(pOwner)->OnStatusChanged(*apChange);
}
} // namespace

bool Acquire()
{
    std::lock_guard lock(g_mutex);
    if (g_users == 0)
    {
        SteamDatagramErrMsg error;
        g_ready = GameNetworkingSockets_Init(nullptr, error);
    }
    if (!g_ready)
        return false;

    ++g_users;
    return true;
}

void Release()
{
    std::lock_guard lock(g_mutex);
    if (g_users == 0)
        return;
    if (--g_users == 0)
    {
        GameNetworkingSockets_Kill();
        g_ready = false;
    }
}

ISteamNetworkingSockets* Sockets()
{
    return SteamNetworkingSockets();
}

void Register(StatusSink* apSink)
{
    std::lock_guard lock(g_mutex);
    g_sinks.insert(apSink);
}

void Unregister(StatusSink* apSink)
{
    std::lock_guard lock(g_mutex);
    g_sinks.erase(apSink);
}

int MakeOptions(StatusSink* apOwner, SteamNetworkingConfigValue_t* apOptions, int aConnectTimeoutMs)
{
    int count = 0;
    apOptions[count++].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&Trampoline));
    apOptions[count++].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, reinterpret_cast<int64>(apOwner));
    apOptions[count++].SetInt32(k_ESteamNetworkingConfig_SendRateMax, kSendRateMax);
    if (aConnectTimeoutMs > 0)
        apOptions[count++].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, aConnectTimeoutMs);
    return count;
}
} // namespace TrueMP::Net::Gns
