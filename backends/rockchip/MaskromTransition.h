#ifndef ROCKCHIP_MASKROM_TRANSITION_H
#define ROCKCHIP_MASKROM_TRANSITION_H
#include "RKComm.h"
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace core::rockchip {
struct ModeDevice { uint32_t location; int mode; };
enum class MaskromResult { Succeeded, UnsupportedMode, CommandFailed, TimedOut, Cancelled, Ambiguous };
struct MaskromHooks {
    std::function<bool(BYTE)> reset;
    std::function<std::vector<ModeDevice>()> scan;
    std::function<bool()> cancelled;
    std::function<void()> waiting;
    std::function<std::chrono::steady_clock::time_point()> now = [] { return std::chrono::steady_clock::now(); };
    std::function<void(std::chrono::milliseconds)> sleep = [](auto duration) { std::this_thread::sleep_for(duration); };
};

// The original serial/PID can disappear in Maskrom. Only the exact USB location
// identifies this transition; another attached Maskrom device cannot confirm it.
inline MaskromResult EnterMaskrom(int mode, uint32_t location, std::chrono::milliseconds timeout,
                                 const MaskromHooks &hooks)
{
    if (hooks.cancelled()) return MaskromResult::Cancelled;
    if (mode != RKUSB_LOADER) return MaskromResult::UnsupportedMode;
    if (!hooks.reset(RST_RESETMASKROM_SUBCODE))
        return hooks.cancelled() ? MaskromResult::Cancelled : MaskromResult::CommandFailed;
    if (hooks.cancelled()) return MaskromResult::Cancelled;
    hooks.waiting();
    const auto deadline = hooks.now() + timeout;
    for (;;) {
        if (hooks.cancelled()) return MaskromResult::Cancelled;
        int matches = 0;
        bool maskrom = false;
        for (const auto &device : hooks.scan()) {
            if (device.location != location) continue;
            ++matches;
            maskrom = device.mode == RKUSB_MASKROM;
        }
        if (hooks.cancelled()) return MaskromResult::Cancelled;
        if (matches > 1) return MaskromResult::Ambiguous;
        if (matches == 1 && maskrom) return MaskromResult::Succeeded;
        if (hooks.now() >= deadline) return MaskromResult::TimedOut;
        hooks.sleep(std::chrono::milliseconds(50));
    }
}
}
#endif
