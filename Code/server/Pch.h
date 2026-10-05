#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <TiltedCore/Platform.hpp>
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Outcome.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <any>
#include <mutex>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <codecvt>
#include <optional>

#include <Server.h>
#include <cxxopts.hpp>

#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <BuildInfo.h>

#include <StringCache.h>
#include <ConsoleRegistry.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <sol/sol.hpp>

using namespace std::chrono_literals;

using TrueMP::Net::ConnectionId_t;
using TiltedPhoques::MakeShared;
using TiltedPhoques::MakeUnique;
using TiltedPhoques::Map;
using TiltedPhoques::ScopedAllocator;
using TrueMP::Net::Server;
using TiltedPhoques::String;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;
using TiltedPhoques::ViewBuffer;

#undef GetClassName

#include <Components.h>
