#pragma once
#include <cstdint>

namespace helengine::x360 {
/// The big-endian 16-byte XAM controller state returned by the pinned Xenia kernel.
struct X360GamepadPacket {
    /// Increments when the controller state changes.
    uint32_t packetNumber;
    /// Native XInput button bits, translated to the engine's abstract buttons.
    uint16_t buttons;
    /// Analog left trigger in the native unsigned 0..255 range.
    uint8_t leftTrigger;
    /// Analog right trigger in the native unsigned 0..255 range.
    uint8_t rightTrigger;
    /// Signed left stick horizontal axis.
    int16_t leftX;
    /// Signed left stick vertical axis, positive upwards.
    int16_t leftY;
    /// Signed right stick horizontal axis.
    int16_t rightX;
    /// Signed right stick vertical axis, positive upwards.
    int16_t rightY;
};
static_assert(sizeof(X360GamepadPacket) == 16, "XAM controller state ABI changed");
}
