// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#if defined(_WIN32)
#define MINGOL_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define MINGOL_PLATFORM_LINUX 1
#else
#error "Mingol supports Windows x64 and Linux only."
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define MINGOL_COMPILER_MSVC 1
#define MINGOL_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define MINGOL_COMPILER_GNU 1
#define MINGOL_NOINLINE __attribute__((noinline))
#else
#error "Unsupported compiler."
#endif

// Symbol visibility for the plugin C ABI (used from the script/plugin module).
#if defined(MINGOL_PLATFORM_WINDOWS)
#define MINGOL_EXPORT extern "C" __declspec(dllexport)
#else
#define MINGOL_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define MINGOL_CONCAT_IMPL(a, b) a##b
#define MINGOL_CONCAT(a, b) MINGOL_CONCAT_IMPL(a, b)
#define MINGOL_UNIQUE(name) MINGOL_CONCAT(name, __COUNTER__)
