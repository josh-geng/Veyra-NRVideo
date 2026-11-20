// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/kbm/Calibration.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace veyra::remoteplay::kbm {

void DeadzoneSweep::start(Time100ns now, float from, float to, float stepPerSecond) {
    start_ = now; from_ = std::clamp(from, 0.f, 0.9f); to_ = std::clamp(to, from_ + 0.05f, 1.f);
    rate_ = std::clamp(stepPerSecond, 0.005f, 0.5f); running_ = true;
}
float DeadzoneSweep::deflection(Time100ns now) const noexcept {
    if (!running_) return 0;
    const float t = float(std::max<Time100ns>(0, now - start_)) / 1e7f;
    return std::min(to_, from_ + rate_ * t);
}
float DeadzoneSweep::mark(Time100ns now, float reactionMs) noexcept {
    const Time100ns adjusted = now - Time100ns(std::clamp(reactionMs, 0.f, 1000.f) * 10000.f);
    const float v = std::max(from_, deflection(std::max(adjusted, start_)));
    running_ = false;
    return v;
}

void TurnRateProbe::begin(float deflection, Time100ns) noexcept { deflection_ = std::clamp(deflection, 0.f, 1.f); marks_.clear(); }
void TurnRateProbe::mark(Time100ns now) noexcept {
    if (!marks_.empty() && now - marks_.back() < 1000000) return; // < 100 ms: a double click, not a revolution
    if (marks_.size() < 16) marks_.push_back(now);
}
std::optional<TurnRateProbe::Sample> TurnRateProbe::result() const {
    if (marks_.size() < 3) return std::nullopt;
    Sample s; s.deflection = deflection_; s.revolutions = int(marks_.size()) - 1;
    s.firstRevolutionSeconds = float(marks_[1] - marks_[0]) / 1e7f;
    // The first revolution can include a ramp-up; the rate uses the later ones.
    s.secondsPerRevolution = float(marks_.back() - marks_[1]) / 1e7f / float(s.revolutions - 1);
    if (!(s.secondsPerRevolution > 0.05f)) return std::nullopt;
    return s;
}

CalibrationReport buildCalibration(float deadzone, std::vector<TurnRateProbe::Sample> samples) {
    CalibrationReport r;
    r.deadzone = std::clamp(deadzone, 0.f, 0.6f);
    std::erase_if(samples, [&](const auto& s) { return !(s.secondsPerRevolution > 0) || s.deflection <= r.deadzone; });
    std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.deflection < b.deflection; });
    samples.erase(std::unique(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return std::abs(a.deflection - b.deflection) < 1e-3f; }), samples.end());
    if (samples.empty()) { r.curve.kind = CurveKind::Linear; r.notes.push_back("no turn-rate samples; only the dead zone was applied"); return r; }
    float maxRate = 0;
    for (const auto& s : samples) maxRate = std::max(maxRate, 1.f / s.secondsPerRevolution);
    r.maxDegreesPerSecond = maxRate * 360.f;
    for (const auto& s : samples)
        if (s.revolutions >= 2 && s.firstRevolutionSeconds > s.secondsPerRevolution * 1.10f) r.timeRamp = true;
    if (r.timeRamp) r.notes.push_back("the first revolution was slower: the game ramps turn speed over time; this cannot be removed by the mapper");
    // Saturation: the first deflection that already reaches ≥ 97 % of the maximum rate.
    for (const auto& s : samples)
        if (1.f / s.secondsPerRevolution >= maxRate * 0.97f) { if (s.deflection < 0.98f) r.saturation = s.deflection; break; }
    if (samples.size() < 3) {
        r.curve.kind = CurveKind::Linear;
        r.notes.push_back("fewer than 3 turn-rate samples; curve left linear");
        return r;
    }
    r.curve.kind = CurveKind::Table;
    r.curve.table.push_back({0.f, 0.f});
    if (r.deadzone > 0.001f) r.curve.table.push_back({r.deadzone, 0.f});
    float running = 0;
    for (const auto& s : samples) {
        float rate = std::min(1.f, (1.f / s.secondsPerRevolution) / maxRate);
        rate = std::max(rate, running); // a measured dip is noise; the table must be monotone
        running = rate;
        r.curve.table.push_back({s.deflection, rate});
        if (rate >= 1.f) break;
    }
    if (r.curve.table.back().rate < 1.f) r.curve.table.back().rate = 1.f;
    if (!r.curve.valid()) { r.curve = {}; r.notes.push_back("measured points were inconsistent; curve left linear"); }
    char buf[96]; std::snprintf(buf, sizeof buf, "max turn rate %.0f deg/s", double(r.maxDegreesPerSecond)); r.notes.push_back(buf);
    return r;
}

void applyCalibration(KbmProfile& p, const CalibrationReport& r) {
    p.mouse.antiDeadzone = r.deadzone;
    p.mouse.curve = r.curve;
    if (r.saturation) p.mouse.maxOutput = std::clamp(*r.saturation, 0.3f, 1.f);
    (void)sanitize(p);
}

LinearityVerdict linearity(std::int64_t slowCounts, std::int64_t fastCounts, float tolerance) {
    LinearityVerdict v;
    const double s = double(std::llabs(slowCounts)), f = double(std::llabs(fastCounts));
    if (s < 50 || f < 50) { v.linear = false; v.ratio = 0; v.message = "too few counts recorded; repeat the test with a full revolution"; return v; }
    v.ratio = float(f / s);
    v.linear = std::abs(v.ratio - 1.f) <= tolerance;
    if (v.linear) v.message = "slow and fast turns need the same mouse distance";
    else if (v.ratio > 1) v.message = "fast turns need more mouse distance: the stick hits the game's turn-rate ceiling (lower sensitivity, or accept the ceiling)";
    else v.message = "fast turns need less mouse distance: the curve over-compensates at high speed (lower the gamma or re-run turn-rate calibration)";
    return v;
}

} // namespace veyra::remoteplay::kbm
