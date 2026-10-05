#pragma once

// Platform and compiler macros used across the game's code.

#if defined(_WIN32)
#define TP_PLATFORM_WINDOWS 1
#else
#define TP_PLATFORM_WINDOWS 0
#endif

#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_X64) || defined(_M_ARM64)
#define TP_PLATFORM_64 1
#define TP_PLATFORM_32 0
#else
#define TP_PLATFORM_64 0
#define TP_PLATFORM_32 1
#endif

// Marks a value or parameter as intentionally unused. Evaluates an expression exactly once, so
// it is also the way to discard a [[nodiscard]] result on purpose.
#define TP_UNUSED(Expression) static_cast<void>(Expression)
