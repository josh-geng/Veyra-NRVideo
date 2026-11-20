// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Relative mouse motion → analog stick deflection.
//
// A PS5 game reading a controller turns the camera at a RATE chosen by the stick
// deflection; a PC game turns by an ANGLE proportional to mouse counts. The best a
// translator can do is make "stick rate" track "mouse speed" as linearly as the game's
// own stick processing allows:
//
//   counts → time-normalized speed (polling-rate independent) → sensitivity / ADS
//   → desired turn rate r∈[0,∞) → inverse of the game's response curve
//   → anti-deadzone → clamp to max deflection (the game's turn-rate ceiling) → int16.
//
// The ceiling is real: once r ≥ 1 the camera cannot turn faster, whatever the mouse
// does. The optional overflow reservoir only redistributes the excess over a bounded
// time; it never exceeds the game's maximum rate.
//
// Velocity estimator: each raw report's displacement is spread over the interval it
// covers (time since the previous report, capped), and speed is the displacement that
// falls inside the window [tLast - W, tLast]. With W ≥ one report interval this gives the
// same output for 125 Hz and 8000 Hz mice moving at the same physical speed, and it
// falls to exactly zero once no report has arrived for ~1.5 report intervals.
#include "KbmTypes.h"
#include <array>
#include <cstdint>
#include <vector>

namespace veyra::remoteplay::kbm {

using Time100ns = std::int64_t; // same clock as remoteplay::HostTime

// Maps a desired normalized turn rate (0..1 = 0..the game's maximum) to a stick
// deflection AFTER the game's dead zone (0..1). Linear: identity. Power: inverse of a
// game curve rate = s^gamma, i.e. s = r^(1/gamma). Table: inverse of measured points.
struct ResponseCurve {
    CurveKind kind = CurveKind::Linear;
    float gamma = 1.0f; // game curve exponent (Power); 1 = linear
    // Measured (deflection, normalized rate) pairs from calibration, sorted by deflection,
    // rates non-decreasing, ending at rate 1. Deflection here is the raw stick value
    // (dead zone included); the table replaces the anti-deadzone when kind == Table.
    struct Point { float deflection = 0, rate = 0; };
    std::vector<Point> table;
    // r ≥ 0 → stick magnitude in [0,1] (before maxOutput). r == 0 → 0.
    float deflectionFor(float rate, float antiDeadzone) const noexcept;
    bool valid() const noexcept;
};

struct MouseConfig {
    float dpi = 800;            // mouse resolution the user reports; normalizes sensitivity
    float sensitivity = 1.0f;   // 1.0: 5 inches/s of hand speed reaches the game's max turn rate
    float sensitivityX = 1.0f, sensitivityY = 1.0f; // per-axis scale
    float adsMultiplier = 0.6f; // applied while ADS is active (L2 held by any source)
    float precisionMultiplier = 0.4f; // applied while the PrecisionModifier key is held
    bool invertY = false;
    float antiDeadzone = 0.12f; // the game's own dead zone, compensated (0..0.6)
    DeadzoneShape deadzoneShape = DeadzoneShape::Radial;
    ResponseCurve curve;
    float maxOutput = 1.0f;     // stick ceiling (0.3..1); lower it if the game saturates early
    float smoothingMs = 0;      // 0 = no smoothing at all (default). EMA time constant otherwise.
    float acceleration = 0;     // optional mouse acceleration: r' = r·(1 + a·r); 0 = off
    float noiseFloor = 0;       // counts/s ignored as sensor noise (0 = none)
    float overflowCarryMs = 0;  // 0 = off; otherwise excess beyond the ceiling is spread over ≤ this time
    DiagonalMode diagonal = DiagonalMode::Circular;
    float minWindowMs = 4;      // ≥ submission interval; the estimator widens it for slow mice
    // Radial-menu (weapon wheel) position mode
    float radialGain = 1.0f;    // full radius per (dpi/2) counts at 1.0
    float radialSelect = 0.35f; // past this radius the stick snaps to full deflection
};

class MouseStick {
public:
    struct Output { float x = 0, y = 0; bool saturated = false; float rate = 0; };
    void configure(const MouseConfig& c) noexcept { cfg_ = c; }
    const MouseConfig& config() const noexcept { return cfg_; }
    // Raw relative report. Not allocation-free only on first use (fixed ring).
    void add(int dx, int dy, Time100ns t) noexcept;
    // Velocity mode. scale = ADS × precision multipliers in effect for this sample.
    Output sample(Time100ns now, float scale) noexcept;
    // Position mode for radial menus: integrates counts into a clamped vector.
    void beginRadial() noexcept { radial_ = {0, 0}; radialActive_ = true; }
    void endRadial() noexcept { radial_ = {0, 0}; radialActive_ = false; }
    bool radialActive() const noexcept { return radialActive_; }
    Output sampleRadial() noexcept;
    // Forget all motion (focus loss, profile switch, device removal).
    void reset() noexcept;
    double reportIntervalMs() const noexcept { return reportInterval_ / 10000.0; }
    bool carrying() const noexcept { return reservoirX_ != 0 || reservoirY_ != 0; }
private:
    struct Event { Time100ns t = 0; Time100ns span = 0; float dx = 0, dy = 0; };
    static constexpr std::size_t Capacity = 512; // 8 kHz × 64 ms
    std::array<Event, Capacity> ring_{};
    std::size_t head_ = 0, count_ = 0;
    Time100ns lastEvent_ = 0, lastSample_ = 0;
    double reportInterval_ = 10000; // 1 ms initial estimate (100 ns units)
    float smoothX_ = 0, smoothY_ = 0, reservoirX_ = 0, reservoirY_ = 0;
    std::array<float, 2> radial_{0, 0};
    bool radialActive_ = false;
    MouseConfig cfg_{};
};

// Shared post-processing for any desired-rate vector (used by tests and calibration).
// (rx, ry): desired normalized rates (signed, may exceed 1). Returns stick in [-1,1].
MouseStick::Output shapeStick(float rx, float ry, const MouseConfig&) noexcept;
std::int16_t toAxis(float v) noexcept; // [-1,1] → int16 with symmetric rounding

} // namespace veyra::remoteplay::kbm
