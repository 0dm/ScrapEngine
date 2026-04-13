#pragma once

#include <cstddef>
#include <utility>

// Dense macros in the spirit of “one header, sharp tools” (EXTERNCDEF + token glue).

// -----------------------------------------------------------------------------
// Token glue / stringify
// -----------------------------------------------------------------------------
#define SCRAP_STR_(x) #x
#define SCRAP_STR(x) SCRAP_STR_(x)

#define SCRAP_GLUE_(a, b) a##b
#define SCRAP_GLUE(a, b) SCRAP_GLUE_(a, b)

/// Stable anonymous symbol per line (macro hygiene).
#define SCRAP_LINE_SYM(prefix) SCRAP_GLUE(prefix, __LINE__)

// -----------------------------------------------------------------------------
// supersecretDLL-style: `extern "C" …;` then repeat tokens (asm blobs, hook arrays, decl+storage pairs).
// -----------------------------------------------------------------------------
#define SCRAP_EXTERNCDEF(...)                                                                      \
    extern "C" __VA_ARGS__;                                                                        \
    __VA_ARGS__;

// -----------------------------------------------------------------------------
// Class policies
// -----------------------------------------------------------------------------
#define SCRAP_NON_COPYABLE(Type_)                                                                  \
    Type_(const Type_&) = delete;                                                                  \
    Type_& operator=(const Type_&) = delete;

#define SCRAP_NON_MOVABLE(Type_)                                                                   \
    Type_(Type_&&) = delete;                                                                       \
    Type_& operator=(Type_&&) = delete;

#define SCRAP_NON_COPY_NON_MOVABLE(Type_)                                                          \
    SCRAP_NON_COPYABLE(Type_)                                                                      \
    SCRAP_NON_MOVABLE(Type_)

// -----------------------------------------------------------------------------
// Branch hints (GCC/Clang; no-op elsewhere)
// -----------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#define SCRAP_LIKELY(x) __builtin_expect(static_cast<bool>(x), 1)
#define SCRAP_UNLIKELY(x) __builtin_expect(static_cast<bool>(x), 0)
#else
#define SCRAP_LIKELY(x) (x)
#define SCRAP_UNLIKELY(x) (x)
#endif

// -----------------------------------------------------------------------------
// “Should never happen” — UB if reached; silences “not all paths return” when used as tail.
// -----------------------------------------------------------------------------
#if defined(__cpp_lib_unreachable) && (__cpp_lib_unreachable >= 202202L)
#define SCRAP_UNREACHABLE() (::std::unreachable())
#elif defined(__GNUC__) || defined(__clang__)
#define SCRAP_UNREACHABLE() (__builtin_unreachable())
#else
#define SCRAP_UNREACHABLE() ((void)0)
#endif

// -----------------------------------------------------------------------------
// Small constexpr-ish helpers (no std dependency beyond what you already have)
// -----------------------------------------------------------------------------
#define SCRAP_IGNORE(expr) ((void)(expr))

/// Elements in a *static* array only (decays if you pass a pointer — don’t).
#define SCRAP_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

#define SCRAP_MIN(a, b) ((a) < (b) ? (a) : (b))
#define SCRAP_MAX(a, b) ((a) > (b) ? (a) : (b))

#define SCRAP_BIT(n) (static_cast<std::size_t>(1) << (n))
