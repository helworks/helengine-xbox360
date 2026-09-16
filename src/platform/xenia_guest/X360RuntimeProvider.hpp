/*
 * Storage and services for the transpiled engine core (csharpcodegen's
 * generic provider contract; the generated tree includes this header because
 * tools/generate-core.ps1 passes codegen-runtime-provider-header).
 *
 * The core is generated with every std container capability off, so every
 * container it uses comes from he_cpp_custom. On this target those are the
 * libstdc++ containers: devkitPPC ships the headers, and they are usable
 * because the core is compiled without -ffreestanding (the generated
 * runtime includes <functional> and <memory> unconditionally, so a
 * freestanding compile of the core is not possible). The few library
 * symbols they need are supplied by the guest itself:
 *
 *   operator new / delete -> the guest heap (cxx_runtime.cpp, heap.c)
 *   memcpy/memmove/memset/memcmp/memchr/strlen/strcmp -> runtime.c
 *   std::__throw_* -> X360RuntimeProvider.cpp, which reports and halts
 *                     instead of throwing, so no unwinder is linked
 *
 * Routing the containers through this header rather than through codegen's
 * std capability flags keeps Fail and the clock as explicit guest hooks and
 * leaves room to replace the containers later without regenerating.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace he_cpp_custom {

using String = std::string;
template <class T> using Vector = std::vector<T>;
template <class K, class V, class H, class E>
using UnorderedMap = std::unordered_map<K, V, H, E>;
template <class T, class H, class E>
using UnorderedSet = std::unordered_set<T, H, E>;
template <class T> using Hash = std::hash<T>;
template <class T> using Function = std::function<T>;
template <class T> using SharedPtr = std::shared_ptr<T>;

/* Reports an unrecoverable managed-runtime failure and stops the guest.
   The core is generated with exceptions off, so this is where a C# throw
   ends up. */
[[noreturn]] void Fail(const char *message);

/* Monotonic microseconds from the PowerPC time base. */
std::uint64_t MonotonicMicroseconds();

} // namespace he_cpp_custom
