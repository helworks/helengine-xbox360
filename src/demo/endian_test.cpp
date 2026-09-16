/*
 * Checks the generated runtime's binary serialization on this console.
 *
 * The Xbox 360 is big-endian, which is the case the generated BinaryReader
 * and BinaryWriter used to get wrong: they assumed the CPU running the
 * program stores the least significant byte first, so on this target every
 * multi-byte value they read or wrote came out byte-swapped, silently. The
 * fix lives in csharpcodegen (runtime/native_endian.hpp); this runs it where
 * the bug would actually appear.
 *
 * The checks are the contract rather than the implementation: a value
 * written for a given wire order must produce exactly those bytes in the
 * stream, and reading them back must return the value.
 */
#include "runtime/native_endian.hpp"
#include "system/io/binary-reader.hpp"
#include "system/io/binary-writer.hpp"
#include "system/io/memory-stream.hpp"

extern "C" {
void hel_log(const char *message);
void hel_log_u32(const char *label, unsigned int value);
}

namespace {

unsigned int g_failures;

void check(const char *what, bool ok) {
    hel_log_u32(what, ok ? 1u : 0u);
    if (!ok) {
        g_failures++;
    }
}

/* The swap rule, asserted for both hosts rather than only this one. */
static_assert(!he_cpp_endian::NeedsSwapBetween(true, true), "LE value, LE host");
static_assert(he_cpp_endian::NeedsSwapBetween(true, false), "LE value, BE host");
static_assert(he_cpp_endian::NeedsSwapBetween(false, true), "BE value, LE host");
static_assert(!he_cpp_endian::NeedsSwapBetween(false, false), "BE value, BE host");

bool wire_matches(bool wire_is_little_endian, const uint8_t *expected) {
    const int32_t sample = 0x01020304;
    MemoryStream stream;
    BinaryWriter writer(stream, wire_is_little_endian);
    writer.WriteInt32(sample);

    stream.SetPosition(0);
    uint8_t wire[sizeof(sample)] = {};
    if (stream.Read(wire, 0, sizeof(wire)) != sizeof(wire)) {
        return false;
    }
    for (size_t index = 0; index < sizeof(wire); ++index) {
        if (wire[index] != expected[index]) {
            hel_log_u32("HEL endian: unexpected byte at", (unsigned int)index);
            hel_log_u32("HEL endian: got", wire[index]);
            hel_log_u32("HEL endian: expected", expected[index]);
            return false;
        }
    }

    stream.SetPosition(0);
    BinaryReader reader(stream, wire_is_little_endian);
    return reader.ReadInt32() == sample;
}

} // namespace

extern "C" unsigned int hel_endian_test(void) {
    g_failures = 0;
    hel_log("HEL endian: binary serialization check start");

    /* If this fails the rest means nothing: the test would be running on a
       little-endian host, where the old code was already correct. */
    check("HEL endian: host is big-endian", !he_cpp_endian::HostIsLittleEndian);

    static const uint8_t big_endian_wire[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t little_endian_wire[] = {0x04, 0x03, 0x02, 0x01};
    check("HEL endian: big-endian wire round trip",
          wire_matches(false, big_endian_wire));
    check("HEL endian: little-endian wire round trip",
          wire_matches(true, little_endian_wire));

    hel_log_u32("HEL endian: failures", g_failures);
    return g_failures;
}
