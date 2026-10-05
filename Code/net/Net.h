#pragma once

// SkyrimTrueMP network library.
//
// A small client/server layer over Valve's GameNetworkingSockets (standalone library).
// It provides what the game needs and nothing more: reliable and unreliable messages,
// payload compression, a synchronised clock, and connection lifecycle callbacks.
//
// Original code for this project (GPLv3-or-later); it replaces the third-party
// "TiltedConnect" library, which carries no licence permitting modification.

#include <cstddef>
#include <cstdint>

namespace TrueMP::Net
{
// Identifies one connection on a server.
using ConnectionId_t = uint32_t;

enum EPacketFlags
{
    kReliable,  // delivered, in order
    kUnreliable // may be lost or arrive out of order
};

// The largest payload one Send() may carry. GameNetworkingSockets limits a message to 512 KiB;
// the rest is headroom for the frame header.
inline constexpr size_t kMaxPayload = 512 * 1024 - 64;

// Application-defined close codes we send to the other side. GameNetworkingSockets reserves
// 1000-1999 for the application.
enum EEndCode : int
{
    kEndNormal = 1000,   // an orderly close by the client
    kEndKicked = 1001,   // the server removed the client
    kEndShutdown = 1002  // the server is stopping
};
} // namespace TrueMP::Net
