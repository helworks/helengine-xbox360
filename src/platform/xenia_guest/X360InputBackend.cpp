#include "X360InputBackend.hpp"
#include "X360GamepadPacket.hpp"
#include "InputGamepadButton.hpp"
#include "X360RuntimeProvider.hpp"

extern "C" {
/// Polls a native controller using the XAM import library, without host input substitutes.
uint32_t XamInputGetState(uint32_t userIndex, uint32_t flags,
                         helengine::x360::X360GamepadPacket* state);
/// Writes one diagnostic only when the native controller connection or buttons change.
void hel_log_u32(const char* label, unsigned int value);
}

namespace helengine::x360 {
X360InputBackend::X360InputBackend()
    : snapshots_{new Array<::InputGamepadState>(4), new Array<::InputGamepadState>(4)}, nextSnapshot_(0) {}

::InputFrameState X360InputBackend::CaptureFrame() {
    Array<::InputGamepadState>* gamepads = snapshots_[nextSnapshot_];
    nextSnapshot_ = 1u - nextSnapshot_;
    for (int32_t index = 0; index < 4; ++index) {
        X360GamepadPacket packet{};
        ::InputGamepadState state{};
        const uint32_t result = XamInputGetState((uint32_t)index, 0, &packet);
        if (result != 0 && result != 0x48fu) {
            he_cpp_custom::Fail("XamInputGetState returned an unexpected controller error");
        }
        state.set_Connected(result == 0);
        if (result == 0) {
            state.set_LeftStickX(packet.leftX);
            state.set_LeftStickY(packet.leftY);
            state.set_RightStickX(packet.rightX);
            state.set_RightStickY(packet.rightY);
            state.set_LeftTrigger((int16_t)((uint32_t)packet.leftTrigger * 32767u / 255u));
            state.set_RightTrigger((int16_t)((uint32_t)packet.rightTrigger * 32767u / 255u));
            state.SetButtonDown(::InputGamepadButton::South, (packet.buttons & 0x1000u) != 0);
            state.SetButtonDown(::InputGamepadButton::East, (packet.buttons & 0x2000u) != 0);
            state.SetButtonDown(::InputGamepadButton::West, (packet.buttons & 0x4000u) != 0);
            state.SetButtonDown(::InputGamepadButton::North, (packet.buttons & 0x8000u) != 0);
            state.SetButtonDown(::InputGamepadButton::LeftShoulder, (packet.buttons & 0x0100u) != 0);
            state.SetButtonDown(::InputGamepadButton::RightShoulder, (packet.buttons & 0x0200u) != 0);
            state.SetButtonDown(::InputGamepadButton::LeftStick, (packet.buttons & 0x0040u) != 0);
            state.SetButtonDown(::InputGamepadButton::RightStick, (packet.buttons & 0x0080u) != 0);
            state.SetButtonDown(::InputGamepadButton::DPadUp, (packet.buttons & 0x0001u) != 0);
            state.SetButtonDown(::InputGamepadButton::DPadDown, (packet.buttons & 0x0002u) != 0);
            state.SetButtonDown(::InputGamepadButton::DPadLeft, (packet.buttons & 0x0004u) != 0);
            state.SetButtonDown(::InputGamepadButton::DPadRight, (packet.buttons & 0x0008u) != 0);
            state.SetButtonDown(::InputGamepadButton::Start, (packet.buttons & 0x0010u) != 0);
            state.SetButtonDown(::InputGamepadButton::Select, (packet.buttons & 0x0020u) != 0);
            state.SetButtonDown(::InputGamepadButton::Home, (packet.buttons & 0x0400u) != 0);
            state.SetButtonDown(::InputGamepadButton::LeftTrigger, packet.leftTrigger >= 30);
            state.SetButtonDown(::InputGamepadButton::RightTrigger, packet.rightTrigger >= 30);
        }
        const ::InputGamepadState& previous = (*snapshots_[nextSnapshot_])[index];
        if (previous.Connected != state.Connected || previous.Buttons != state.Buttons) {
            hel_log_u32("HEL input: controller slot", (unsigned int)index);
            hel_log_u32("HEL input: connected", state.Connected ? 1u : 0u);
            hel_log_u32("HEL input: abstract button mask", (unsigned int)state.Buttons);
        }
        (*gamepads)[index] = state;
    }
    ::InputFrameState frame{};
    frame.set_Gamepads(gamepads);
    frame.set_GamepadCount(4);
    return frame;
}
}
