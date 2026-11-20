// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Thread-safe keyboard/mouse → virtual controller engine.
//
// Producers: the raw-input thread calls key()/mouseMove()/wheel() as reports arrive
// (bounded, allocation-free, O(1) per event). Consumer: the Remote Play session owner
// calls compose() immediately before every controller submission, so the state that
// reaches Chiaki is sampled at submission time rather than at a UI timer tick. The UI
// thread only edits profiles and reads stats. One mutex; never held across I/O.
//
// Safety contract (stuck-input prevention):
//  * setEnabled(false) / releaseAll() / setProfiles() neutralize every key, toggle,
//    wheel pulse, stick slew and mouse history at once.
//  * Keys still physically held across a profile switch are suppressed until released,
//    so the new profile never sees a "press" the user did not make.
//  * A press shorter than one submission interval is latched and still reaches the
//    console for exactly one submission, followed by its release.
#include "KbmProfile.h"
#include "MouseStick.h"
#include "veyra/remoteplay/Types.h"
#include <array>
#include <bitset>
#include <mutex>
#include <optional>

namespace veyra::remoteplay::kbm {

struct KbmStats {
    bool enabled = false;
    InputMode mode = InputMode::Hybrid;
    int activeProfile = 0;
    std::string profileName;
    // Measured over the last completed second.
    double mouseEventsPerSecond = 0, keyEventsPerSecond = 0;
    double mouseReportIntervalMs = 0;     // estimated from report spacing (≈ 1 / polling rate while moving)
    double composeAverageUs = 0, composeMaxUs = 0;     // input processing time per submission
    double submitIntervalAverageMs = 0, submitIntervalMaxMs = 0; // spacing of compose() calls
    double saturatedPercent = 0;          // submissions where the mouse asked for more than the ceiling
    std::uint64_t submissions = 0;
    // Last KBM contribution (for the live visualizer); stick range [-1,1], triggers [0,1].
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0, l2 = 0, r2 = 0;
    std::uint32_t buttons = 0;
    int heldInputs = 0;
    bool radial = false, ads = false, precision = false, carrying = false, overrideActive = false;
    std::int64_t mouseCountsX = 0, mouseCountsY = 0; // running totals (calibration linearity test)
};

class KbmEngine {
public:
    KbmEngine();
    // Replaces all profiles; releases everything. Invalid input is sanitized, never trusted.
    void setProfiles(ProfileSet);
    ProfileSet profiles() const;
    void selectProfile(int index); // releases everything; held keys suppressed until released
    void setMode(InputMode, StickPriority);
    // Capture active (window focused, user engaged). false releases everything immediately.
    void setEnabled(bool);
    bool enabled() const;
    // Neutral now (focus loss, device removal, session restart). Keeps enabled state.
    void releaseAll();

    void key(InputCode, bool down, Time100ns t);
    void mouseMove(int dx, int dy, Time100ns t);
    void wheel(InputCode notch, int steps, Time100ns t); // WheelUp/Down/Left/Right, steps ≥ 1

    // Calibration: forces the right stick to this deflection (x, y in [-1,1]) while set.
    void setRightStickOverride(std::optional<std::array<float, 2>>);

    // Called by the session owner before each submission. Pure function of the inputs
    // so far and `now`; merges with the physical pad according to the input mode.
    ControllerState compose(const ControllerState& pad, Time100ns now);
    KbmStats stats() const;

private:
    void applyProfileLocked(int index);
    void releaseLocked();
    void pressEdgeLocked(InputCode, Time100ns);

    mutable std::mutex mutex_;
    ProfileSet set_;
    KbmProfile active_;
    std::vector<bool> toggled_;          // per binding of the active profile
    std::bitset<code::Max> down_, latched_, suppressed_;
    std::array<std::uint64_t, code::Max> pressOrder_{};
    std::array<Time100ns, code::Max> pressedAt_{};
    std::uint64_t order_ = 0;
    struct WheelState { int pending = 0; bool on = false; Time100ns phaseEnd = 0; };
    std::array<WheelState, 4> wheels_{};
    MouseStick mouse_;
    float leftX_ = 0, leftY_ = 0, l2_ = 0, r2_ = 0; // slewed keyboard outputs
    Time100ns lastCompose_ = 0;
    std::optional<std::array<float, 2>> override_;
    bool enabled_ = false;
    std::optional<int> pendingSwitch_;
    // stats
    KbmStats stats_;
    Time100ns windowStart_ = 0;
    std::uint64_t mouseEvents_ = 0, keyEvents_ = 0, samples_ = 0, saturated_ = 0, gaps_ = 0;
    double composeSumUs_ = 0, composeMaxUs_ = 0, gapSumMs_ = 0, gapMaxMs_ = 0;
};

// Hybrid merge used by compose (exposed for tests).
ControllerState mergeStates(const ControllerState& pad, const ControllerState& kbm, InputMode, StickPriority);

} // namespace veyra::remoteplay::kbm
