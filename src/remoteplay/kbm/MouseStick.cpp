// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/kbm/MouseStick.h"
#include <algorithm>
#include <cmath>

namespace veyra::remoteplay::kbm {
namespace {
constexpr double Ms = 10000.0; // 100 ns ticks per millisecond
constexpr double Second = 10000000.0;
float clamp01(float v) noexcept { return std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 1.f); }
} // namespace

bool ResponseCurve::valid() const noexcept {
    if (!std::isfinite(gamma) || gamma < 0.2f || gamma > 5.f) return false;
    if (kind != CurveKind::Table) return true;
    if (table.size() < 2 || table.size() > 32) return false;
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto& p = table[i];
        if (!std::isfinite(p.deflection) || !std::isfinite(p.rate) || p.deflection < 0 || p.deflection > 1 || p.rate < 0 || p.rate > 1) return false;
        if (i && (p.deflection <= table[i - 1].deflection || p.rate < table[i - 1].rate)) return false;
    }
    return table.back().rate > 0.99f && table.front().rate < 0.01f;
}

float ResponseCurve::deflectionFor(float rate, float antiDeadzone) const noexcept {
    if (!(rate > 0)) return 0;
    const float r = std::min(rate, 1.f);
    const float adz = std::clamp(antiDeadzone, 0.f, 0.95f);
    switch (kind) {
    case CurveKind::Linear: return adz + (1 - adz) * r;
    case CurveKind::Power: return adz + (1 - adz) * std::pow(r, 1.f / std::clamp(gamma, 0.2f, 5.f));
    case CurveKind::Table: {
        if (!valid()) return adz + (1 - adz) * r; // fail safe: behave linearly, never output garbage
        for (std::size_t i = 1; i < table.size(); ++i) {
            const auto& a = table[i - 1];
            const auto& b = table[i];
            if (r <= b.rate) {
                if (b.rate - a.rate < 1e-6f) return b.deflection;
                return a.deflection + (b.deflection - a.deflection) * (r - a.rate) / (b.rate - a.rate);
            }
        }
        return table.back().deflection;
    }
    }
    return 0;
}

namespace {
// Normalized rate the game produces at a given stick magnitude (inverse of deflectionFor).
float rateAt(const ResponseCurve& c, float deflection, float antiDeadzone) noexcept {
    const float adz = std::clamp(antiDeadzone, 0.f, 0.95f);
    const float d = clamp01(deflection);
    if (c.kind == CurveKind::Table && c.valid()) {
        if (d <= c.table.front().deflection) return c.table.front().rate;
        for (std::size_t i = 1; i < c.table.size(); ++i) {
            const auto& a = c.table[i - 1];
            const auto& b = c.table[i];
            if (d <= b.deflection) return a.rate + (b.rate - a.rate) * (d - a.deflection) / (b.deflection - a.deflection);
        }
        return 1;
    }
    const float lin = clamp01((d - adz) / (1 - adz));
    return c.kind == CurveKind::Power ? std::pow(lin, std::clamp(c.gamma, 0.2f, 5.f)) : lin;
}
float shapeMagnitude(float m, const MouseConfig& cfg, bool& saturated) noexcept {
    const float maxOut = std::clamp(cfg.maxOutput, 0.05f, 1.f);
    float s = cfg.curve.deflectionFor(m, cfg.antiDeadzone);
    if (s >= maxOut - 1e-6f && m > 0) { saturated = saturated || m > rateAt(cfg.curve, maxOut, cfg.antiDeadzone) + 1e-4f; s = maxOut; }
    return s;
}
} // namespace

std::int16_t toAxis(float v) noexcept {
    const float c = std::clamp(std::isfinite(v) ? v : 0.f, -1.f, 1.f);
    return static_cast<std::int16_t>(std::lround(c * 32767.f));
}

MouseStick::Output shapeStick(float rx, float ry, const MouseConfig& cfg) noexcept {
    MouseStick::Output out;
    if (!std::isfinite(rx) || !std::isfinite(ry)) return out;
    if (cfg.deadzoneShape == DeadzoneShape::Axial) {
        bool sat = false;
        const float sx = shapeMagnitude(std::abs(rx), cfg, sat);
        const float sy = shapeMagnitude(std::abs(ry), cfg, sat);
        out.x = std::copysign(sx, rx); out.y = std::copysign(sy, ry);
        if (rx == 0) out.x = 0;
        if (ry == 0) out.y = 0;
        out.saturated = sat; out.rate = std::hypot(rx, ry);
        return out;
    }
    const float m = std::hypot(rx, ry);
    if (m < 1e-7f) return out;
    bool sat = false;
    const float s = shapeMagnitude(m, cfg, sat);
    float dx = rx / m, dy = ry / m;
    if (cfg.diagonal == DiagonalMode::Square) {
        const float k = std::max(std::abs(dx), std::abs(dy));
        if (k > 1e-6f) { dx /= k; dy /= k; }
    }
    out.x = std::clamp(dx * s, -1.f, 1.f); out.y = std::clamp(dy * s, -1.f, 1.f);
    out.saturated = sat; out.rate = m;
    return out;
}

void MouseStick::add(int dx, int dy, Time100ns t) noexcept {
    if (dx == 0 && dy == 0) return;
    if (radialActive_) {
        const float full = std::max(50.f, cfg_.dpi * 0.5f) / std::clamp(cfg_.radialGain, 0.1f, 10.f);
        radial_[0] += float(dx) / full;
        radial_[1] += float(dy) / full;
        const float m = std::hypot(radial_[0], radial_[1]);
        if (m > 1) { radial_[0] /= m; radial_[1] /= m; }
        return; // motion inside a menu never turns the camera afterwards
    }
    if (count_ && t < lastEvent_) t = lastEvent_; // monotonic, even if a caller's clock steps
    const Time100ns gap = count_ ? t - lastEvent_ : 0; // 8 kHz mice report every 1250 ticks
    Time100ns span;
    if (!count_ || lastEvent_ == 0 || gap > 200000) span = Time100ns(reportInterval_); // first report after rest
    else if (gap < 300) span = Time100ns(reportInterval_);                              // batched delivery (< 30 µs apart)
    else {
        reportInterval_ = std::clamp(reportInterval_ * 0.9 + double(gap) * 0.1, 1250.0, 160000.0);
        span = std::min<Time100ns>(gap, Time100ns(reportInterval_ * 2));
    }
    span = std::max<Time100ns>(span, 1000);
    ring_[head_] = {t, span, float(dx), float(dy)};
    head_ = (head_ + 1) % Capacity;
    count_ = std::min(count_ + 1, Capacity);
    lastEvent_ = t;
}

MouseStick::Output MouseStick::sample(Time100ns now, float scale) noexcept {
    const Time100ns dtSample = lastSample_ && now > lastSample_ ? std::min<Time100ns>(now - lastSample_, Time100ns(50 * Ms)) : Time100ns(cfg_.minWindowMs * Ms);
    lastSample_ = now;
    const double ri = reportInterval_;
    double window = std::max(double(std::clamp(cfg_.minWindowMs, 1.f, 20.f)) * Ms, 1.5 * ri);
    window = std::clamp(std::max(window, std::min(double(dtSample), 25 * Ms)), 1 * Ms, 30 * Ms);
    const double stopAfter = 1.5 * ri + 0.5 * Ms;
    float vx = 0, vy = 0; // counts per second
    if (count_ && double(now - lastEvent_) <= stopAfter) {
        const double end = double(lastEvent_), begin = end - window;
        double sx = 0, sy = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& e = ring_[(head_ + Capacity - 1 - i) % Capacity];
            const double t1 = double(e.t), t0 = t1 - double(e.span);
            if (t1 <= begin) break;
            const double overlap = std::min(t1, end) - std::max(t0, begin);
            if (overlap > 0) { const double f = overlap / double(e.span); sx += e.dx * f; sy += e.dy * f; }
        }
        vx = float(sx * Second / window); vy = float(sy * Second / window);
    }
    if (cfg_.noiseFloor > 0 && std::hypot(vx, vy) < cfg_.noiseFloor) vx = vy = 0;
    const float full = std::max(1.f, std::clamp(cfg_.dpi, 50.f, 64000.f) * 5.f) / std::clamp(cfg_.sensitivity, 0.01f, 50.f);
    const float k = std::clamp(scale, 0.01f, 10.f);
    float rx = vx / full * std::clamp(cfg_.sensitivityX, 0.01f, 10.f) * k;
    float ry = vy / full * std::clamp(cfg_.sensitivityY, 0.01f, 10.f) * k * (cfg_.invertY ? -1.f : 1.f);
    if (cfg_.acceleration > 0) {
        const float m = std::hypot(rx, ry);
        const float g = 1 + std::clamp(cfg_.acceleration, 0.f, 4.f) * m;
        rx *= g; ry *= g;
    }
    // Overflow reservoir: bounded redistribution of motion beyond the turn-rate ceiling.
    if (cfg_.overflowCarryMs > 0) {
        const float ceil = std::max(1e-3f, rateAt(cfg_.curve, std::clamp(cfg_.maxOutput, 0.05f, 1.f), cfg_.antiDeadzone));
        const float dt = float(dtSample);
        if (rx * reservoirX_ + ry * reservoirY_ < 0) reservoirX_ = reservoirY_ = 0; // reversed: drop stale excess
        float tx = rx + reservoirX_ / dt, ty = ry + reservoirY_ / dt;
        const float m = std::hypot(tx, ty);
        if (m > ceil) {
            const float ox = tx * ceil / m, oy = ty * ceil / m;
            reservoirX_ = (tx - ox) * dt; reservoirY_ = (ty - oy) * dt;
            tx = ox; ty = oy;
            const float cap = ceil * float(std::clamp(cfg_.overflowCarryMs, 0.f, 300.f) * Ms);
            const float rm = std::hypot(reservoirX_, reservoirY_);
            if (rm > cap) { reservoirX_ *= cap / rm; reservoirY_ *= cap / rm; }
        } else reservoirX_ = reservoirY_ = 0;
        rx = tx; ry = ty;
    } else reservoirX_ = reservoirY_ = 0;
    if (cfg_.smoothingMs > 0) {
        const float a = 1 - std::exp(-float(dtSample) / float(std::clamp(cfg_.smoothingMs, 0.1f, 100.f) * Ms));
        smoothX_ += (rx - smoothX_) * a; smoothY_ += (ry - smoothY_) * a;
        if (rx == 0 && ry == 0 && std::hypot(smoothX_, smoothY_) < 0.01f) smoothX_ = smoothY_ = 0; // settle to exact zero
        rx = smoothX_; ry = smoothY_;
    } else smoothX_ = smoothY_ = 0;
    return shapeStick(rx, ry, cfg_);
}

MouseStick::Output MouseStick::sampleRadial() noexcept {
    Output out;
    const float m = std::hypot(radial_[0], radial_[1]);
    if (m < 1e-6f) return out;
    const float sel = std::clamp(cfg_.radialSelect, 0.f, 0.95f);
    const float s = m >= sel ? 1.f : m;
    out.x = radial_[0] / m * s; out.y = radial_[1] / m * s;
    out.rate = m;
    return out;
}

void MouseStick::reset() noexcept {
    head_ = count_ = 0; lastEvent_ = lastSample_ = 0;
    smoothX_ = smoothY_ = reservoirX_ = reservoirY_ = 0;
    radial_ = {0, 0}; radialActive_ = false;
}

} // namespace veyra::remoteplay::kbm
