// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Guided manual calibration of the mouse → stick translation.
//
// Fully automatic calibration would need to read camera rotation out of the video
// stream; that is not reliable across scenes, HUDs and motion blur, so Veyra does not
// pretend to do it. Instead the engine drives the right stick with known deflections
// and the player marks what they see:
//
//  1. Dead zone:  the deflection rises slowly; the player presses "moved" the moment
//                 the camera starts turning. A reaction-time allowance is subtracted.
//  2. Turn rate:  for several fixed deflections, the player marks a landmark passing the
//                 crosshair on consecutive revolutions. Seconds per revolution give the
//                 game's rate curve, its saturation point and any time-based ramp.
//  3. Linearity:  the player turns exactly one revolution slowly, then quickly, with the
//                 mouse; equal counts mean the curve is linear, unequal counts show
//                 residual curve error or saturation.
#include "KbmProfile.h"
#include <optional>
#include <string>
#include <vector>

namespace veyra::remoteplay::kbm {

class DeadzoneSweep {
public:
    void start(Time100ns now, float from = 0.f, float to = 0.6f, float stepPerSecond = 0.04f);
    bool running() const noexcept { return running_; }
    float deflection(Time100ns now) const noexcept; // drive the right stick X with this
    // Player saw movement now. Returns the estimated dead-zone edge (reaction-corrected).
    float mark(Time100ns now, float reactionMs = 220.f) noexcept;
    void cancel() noexcept { running_ = false; }
    bool finishedWithoutMark(Time100ns now) const noexcept { return running_ && deflection(now) >= to_; }
private:
    Time100ns start_ = 0; float from_ = 0, to_ = 0.6f, rate_ = 0.04f; bool running_ = false;
};

class TurnRateProbe {
public:
    struct Sample { float deflection = 0; float secondsPerRevolution = 0; float firstRevolutionSeconds = 0; int revolutions = 0; };
    void begin(float deflection, Time100ns now) noexcept;
    float deflection() const noexcept { return deflection_; }
    void mark(Time100ns now) noexcept;          // landmark crossed the crosshair
    int marks() const noexcept { return int(marks_.size()); }
    std::optional<Sample> result() const;       // needs ≥ 3 marks (2 revolutions)
private:
    float deflection_ = 0; std::vector<Time100ns> marks_;
};

struct CalibrationReport {
    float deadzone = 0;                 // estimated game dead zone (raw deflection)
    std::optional<float> saturation;    // deflection beyond which the turn rate stops rising
    bool timeRamp = false;              // first revolution measurably slower: the game accelerates over time
    float maxDegreesPerSecond = 0;
    ResponseCurve curve;                // CurveKind::Table when enough points were measured
    std::vector<std::string> notes;
};
// Builds the inverse table from (deflection, seconds per revolution) samples.
CalibrationReport buildCalibration(float deadzone, std::vector<TurnRateProbe::Sample> samples);
// Applies a report to a profile's mouse settings (anti-deadzone, max output, curve).
void applyCalibration(KbmProfile&, const CalibrationReport&);

struct LinearityVerdict { float ratio = 1; bool linear = true; std::string message; };
// Counts the player needed for one revolution moving slowly vs quickly.
LinearityVerdict linearity(std::int64_t slowCounts, std::int64_t fastCounts, float tolerance = 0.12f);

} // namespace veyra::remoteplay::kbm
