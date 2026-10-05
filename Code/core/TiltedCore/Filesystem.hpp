#pragma once

#include "Platform.hpp"
#include "Stl.hpp"

#include <filesystem>

namespace TiltedPhoques
{
// The whole contents of a file, or an empty string if it cannot be read.
[[nodiscard]] String LoadFile(const std::filesystem::path& acPath);

// Writes (replacing) a file. Returns false if it could not be written in full.
bool SaveFile(const std::filesystem::path& acPath, const String& acData);

// The directory holding the running executable.
[[nodiscard]] std::filesystem::path GetPath();
} // namespace TiltedPhoques

#include "Filesystem.inl"
