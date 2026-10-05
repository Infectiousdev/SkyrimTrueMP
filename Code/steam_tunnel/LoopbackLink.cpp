#include "LoopbackLink.h"

#include <set>

namespace TrueMP::Steam
{
namespace
{
struct Event
{
    enum class Kind
    {
        kConnected,
        kDisconnected,
        kDatagram
    } Type;
    PeerId Peer;
    std::vector<uint8_t> Data;
};
} // namespace

struct LoopbackNetwork::Impl
{
    struct Endpoint : IPeerLink
    {
        Endpoint(Impl& aOwner, PeerId aId)
            : Owner(aOwner)
            , Id(aId)
        {
        }

        void Poll(ILinkListener& aListener) override
        {
            std::deque<Event> events;
            {
                std::lock_guard lock(Owner.Mutex);
                events.swap(Inbox);
            }
            for (const auto& event : events)
            {
                switch (event.Type)
                {
                case Event::Kind::kConnected: aListener.OnPeerConnected(event.Peer); break;
                case Event::Kind::kDisconnected: aListener.OnPeerDisconnected(event.Peer); break;
                case Event::Kind::kDatagram: aListener.OnDatagram(event.Peer, event.Data.data(), event.Data.size()); break;
                }
            }
        }

        bool Send(PeerId aPeer, const uint8_t* apData, size_t aSize) override
        {
            std::lock_guard lock(Owner.Mutex);
            Endpoint* pTarget = Owner.Find(aPeer);
            if (!pTarget || !pTarget->Linked.count(Id) || !Linked.count(aPeer))
                return false;
            pTarget->Inbox.push_back({Event::Kind::kDatagram, Id, std::vector<uint8_t>(apData, apData + aSize)});
            return true;
        }

        void Disconnect(PeerId aPeer) override
        {
            std::lock_guard lock(Owner.Mutex);
            Endpoint* pTarget = Owner.Find(aPeer);
            Linked.erase(aPeer);
            if (pTarget && pTarget->Linked.erase(Id))
                pTarget->Inbox.push_back({Event::Kind::kDisconnected, Id, {}});
        }

        Impl& Owner;
        PeerId Id;
        std::deque<Event> Inbox;
        std::set<PeerId> Linked; // peers this endpoint currently has a link to
    };

    Endpoint* Find(PeerId aId)
    {
        const auto it = Endpoints.find(aId);
        return it == Endpoints.end() ? nullptr : it->second;
    }

    std::mutex Mutex;
    std::map<PeerId, Endpoint*> Endpoints;
    std::unique_ptr<Endpoint> HostEndpoint;
};

LoopbackNetwork::LoopbackNetwork()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->HostEndpoint = std::make_unique<Impl::Endpoint>(*m_impl, kHostId);
    m_impl->Endpoints[kHostId] = m_impl->HostEndpoint.get();
}

LoopbackNetwork::~LoopbackNetwork() = default;

IPeerLink& LoopbackNetwork::Host()
{
    return *m_impl->HostEndpoint;
}

std::unique_ptr<IPeerLink> LoopbackNetwork::Connect(PeerId aClient)
{
    // The endpoint removes itself from the network when destroyed.
    struct ClientEndpoint final : Impl::Endpoint
    {
        using Impl::Endpoint::Endpoint;
        ~ClientEndpoint() override
        {
            std::lock_guard lock(Owner.Mutex);
            if (Impl::Endpoint* pHost = Owner.Find(kHostId); pHost && pHost->Linked.erase(Id))
                pHost->Inbox.push_back({Event::Kind::kDisconnected, Id, {}});
            Owner.Endpoints.erase(Id);
        }
    };

    auto client = std::make_unique<ClientEndpoint>(*m_impl, aClient);
    {
        std::lock_guard lock(m_impl->Mutex);
        Impl::Endpoint* pHost = m_impl->HostEndpoint.get();
        m_impl->Endpoints[aClient] = client.get();
        client->Linked.insert(kHostId);
        pHost->Linked.insert(aClient);
        pHost->Inbox.push_back({Event::Kind::kConnected, aClient, {}});
        client->Inbox.push_back({Event::Kind::kConnected, kHostId, {}});
    }
    return client;
}
} // namespace TrueMP::Steam
