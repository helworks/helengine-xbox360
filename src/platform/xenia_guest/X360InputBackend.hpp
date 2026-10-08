#pragma once
#include "IInputBackend.hpp"
#include "InputFrameState.hpp"
#include "InputGamepadState.hpp"
#include "runtime/array.hpp"

namespace helengine::x360 {
/// Captures the four real XAM controller slots for the authored engine input system.
class X360InputBackend final : public ::IInputBackend {
    /// Alternating arrays keep InputSystem's preceding frame intact for pressed/released edges.
    Array<::InputGamepadState>* snapshots_[2];
    /// Selects the array to populate on the next poll.
    uint32_t nextSnapshot_;
public:
    /// Allocates the fixed controller array once, before the first authored update.
    X360InputBackend();
    /// Polls all XAM slots, preserving disconnected slots and abstract button edges.
    ::InputFrameState CaptureFrame() override;
};
}
