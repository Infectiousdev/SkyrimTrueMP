#pragma once

// Definitions for Filesystem.hpp, kept inline so the library needs no compiled code.

#include <fstream>
#include <iterator>

// Deliberately not <windows.h>: this header is included widely, and windows.h defines min/max macros
// that break unrelated code. The C runtime keeps the executable's path for us.
#include <cstdlib>

namespace TiltedPhoques
{
inline String LoadFile(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return String();

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0)
        return String();
    file.seekg(0, std::ios::beg);

    String contents(static_cast<size_t>(size), '\0');
    file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (file.gcount() != static_cast<std::streamsize>(contents.size()))
        return String();
    return contents;
}

inline bool SaveFile(const std::filesystem::path& acPath, const String& acData)
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;

    file.write(acData.data(), static_cast<std::streamsize>(acData.size()));
    return static_cast<bool>(file);
}

inline std::filesystem::path GetPath()
{
#if TP_PLATFORM_WINDOWS
    wchar_t* pExecutable = nullptr;
    if (_get_wpgmptr(&pExecutable) != 0 || !pExecutable || !*pExecutable)
        return std::filesystem::current_path();
    return std::filesystem::path(pExecutable).parent_path();
#else
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : executable.parent_path();
#endif
}
} // namespace TiltedPhoques
