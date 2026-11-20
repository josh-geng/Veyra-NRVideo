// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/kbm/KbmEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace veyra::remoteplay::kbm {
namespace {
constexpr double Ms = 10000.0;
std::uint32_t buttonBit(Target t) noexcept {
    using B = ControllerState::Button;
    switch (t) {
    case Target::Cross: return B::Cross; case Target::Circle: return B::Circle; case Target::Square: return B::Square;
    case Target::Triangle: return B::Triangle; case Target::L1: return B::L1; case Target::R1: return B::R1;
    case Target::L3: return B::L3; case Target::R3: return B::R3; case Target::DpadUp: return B::Up;
    case Target::DpadDown: return B::Down; case Target::DpadLeft: return B::Left; case Target::DpadRight: return B::Right;
    case Target::Options: return B::Options; case Target::Create: return B::Share; case Target::Touchpad: return B::Touchpad;
    case Target::PS: return B::PS;
    default: return 0;
    }
}
float approach(float c, float t, float step) noexcept {
    if (!(step < 1e9f)) return t;
    return c < t ? std::min(t, c + step) : std::max(t, c - step);
}
float axisF(std::int16_t v) noexcept { return float(v) / 32767.f; }
float mag(std::int16_t x, std::int16_t y) noexcept { return std::hypot(axisF(x), axisF(y)); }
} // namespace

ControllerState mergeStates(const ControllerState& pad, const ControllerState& kbm, InputMode mode, StickPriority priority) {
    if (mode == InputMode::ControllerOnly) return pad;
    if (mode == InputMode::KeyboardMouse || !pad.inputActive) return kbm;
    if (!kbm.inputActive) return pad;
    ControllerState out = pad; // touch, motion and sensor batches stay the pad's own
    out.inputActive = true;
    out.buttons = pad.buttons | kbm.buttons;
    out.l2 = std::max(pad.l2, kbm.l2);
    out.r2 = std::max(pad.r2, kbm.r2);
    // One source per stick per submission: summing a drifting pad stick with a mouse
    // would make aim depend on where the thumb happens to rest.
    constexpr float padDeflected = 0.15f; // above typical DualSense resting drift
    const auto pick = [&](std::int16_t px, std::int16_t py, std::int16_t kx, std::int16_t ky, std::int16_t& ox, std::int16_t& oy) {
        const float pm = mag(px, py), km = mag(kx, ky);
        bool usePad;
        switch (priority) {
        case StickPriority::Controller: usePad = pm > padDeflected; break;
        case StickPriority::KeyboardMouse: usePad = km <= 0.f; break;
        default: usePad = pm > padDeflected && pm >= km; break;
        }
        ox = usePad ? px : kx; oy = usePad ? py : ky;
    };
    pick(pad.leftX, pad.leftY, kbm.leftX, kbm.leftY, out.leftX, out.leftY);
    pick(pad.rightX, pad.rightY, kbm.rightX, kbm.rightY, out.rightX, out.rightY);
    return out;
}

KbmEngine::KbmEngine() { setProfiles(defaultProfiles()); }

void KbmEngine::setProfiles(ProfileSet s) {
    (void)sanitize(s);
    std::lock_guard lock(mutex_);
    set_ = std::move(s);
    releaseLocked();
    applyProfileLocked(set_.active);
}

ProfileSet KbmEngine::profiles() const { std::lock_guard lock(mutex_); return set_; }

void KbmEngine::selectProfile(int index) {
    std::lock_guard lock(mutex_);
    if (index < 0 || index >= int(set_.profiles.size())) return;
    applyProfileLocked(index);
}

void KbmEngine::setMode(InputMode mode, StickPriority priority) {
    std::lock_guard lock(mutex_);
    if (mode != set_.mode) releaseLocked();
    set_.mode = mode; set_.priority = priority;
}

void KbmEngine::setEnabled(bool on) {
    std::lock_guard lock(mutex_);
    if (on == enabled_) return;
    enabled_ = on;
    releaseLocked();
}
bool KbmEngine::enabled() const { std::lock_guard lock(mutex_); return enabled_; }
void KbmEngine::releaseAll() { std::lock_guard lock(mutex_); releaseLocked(); }

void KbmEngine::releaseLocked() {
    down_.reset(); latched_.reset(); suppressed_.reset();
    std::fill(toggled_.begin(), toggled_.end(), false);
    wheels_ = {};
    mouse_.reset();
    leftX_ = leftY_ = l2_ = r2_ = 0;
    pendingSwitch_.reset();
}

void KbmEngine::applyProfileLocked(int index) {
    const auto held = down_;
    releaseLocked();
    down_ = held;
    suppressed_ = held; // still physically held: ignored until released
    set_.active = index;
    active_ = set_.profiles[std::size_t(index)];
    toggled_.assign(active_.bindings.size(), false);
    mouse_.configure(active_.mouse);
}

void KbmEngine::pressEdgeLocked(InputCode c, Time100ns) {
    if (suppressed_[c]) return;
    for (std::size_t i = 0; i < active_.bindings.size(); ++i) {
        const auto& b = active_.bindings[i];
        if (b.key != c) continue;
        if (b.modifier && !down_[b.modifier]) continue;
        if (!b.modifier && std::any_of(active_.bindings.begin(), active_.bindings.end(), [&](const Binding& o) { return o.key == c && o.modifier && down_[o.modifier]; })) continue;
        if (b.behavior == Behavior::Toggle) toggled_[i] = !toggled_[i];
        if (b.target == Target::SwitchProfile) pendingSwitch_ = int(b.value);
    }
}

void KbmEngine::key(InputCode c, bool down, Time100ns t) {
    if (!validCode(c) || isWheel(c)) return;
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++keyEvents_;
    if (down) {
        if (down_[c]) return; // auto-repeat
        down_[c] = true; latched_[c] = true;
        pressOrder_[c] = ++order_; pressedAt_[c] = t;
        pressEdgeLocked(c, t);
    } else {
        down_[c] = false;
        suppressed_[c] = false;
    }
    if (pendingSwitch_) {
        const int target = *pendingSwitch_;
        pendingSwitch_.reset();
        if (target >= 0 && target < int(set_.profiles.size()) && target != set_.active) applyProfileLocked(target);
    }
}

void KbmEngine::mouseMove(int dx, int dy, Time100ns t) {
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++mouseEvents_;
    stats_.mouseCountsX += dx; stats_.mouseCountsY += dy;
    mouse_.add(dx, dy, t);
}

void KbmEngine::wheel(InputCode notch, int steps, Time100ns t) {
    if (!isWheel(notch) || steps <= 0) return;
    std::lock_guard lock(mutex_);
    if (!enabled_) return;
    ++keyEvents_;
    auto& w = wheels_[notch - code::WheelUp];
    w.pending = std::min(w.pending + steps, 8); // a fast spin is not a queue of 40 presses
    for (int i = 0; i < std::min(steps, 8); ++i) pressEdgeLocked(notch, t);
    if (pendingSwitch_) {
        const int target = *pendingSwitch_;
        pendingSwitch_.reset();
        if (target >= 0 && target < int(set_.profiles.size()) && target != set_.active) applyProfileLocked(target);
    }
}

void KbmEngine::setRightStickOverride(std::optional<std::array<float, 2>> v) {
    std::lock_guard lock(mutex_);
    if (v) { (*v)[0] = std::clamp(std::isfinite((*v)[0]) ? (*v)[0] : 0.f, -1.f, 1.f); (*v)[1] = std::clamp(std::isfinite((*v)[1]) ? (*v)[1] : 0.f, -1.f, 1.f); }
    override_ = v;
}

ControllerState KbmEngine::compose(const ControllerState& pad, Time100ns now) {
    const auto started = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    double gapMs = 0;
    if (lastCompose_ && now > lastCompose_) { gapMs = double(now - lastCompose_) / Ms; gapSumMs_ += gapMs; gapMaxMs_ = std::max(gapMaxMs_, gapMs); ++gaps_; }
    lastCompose_ = now;
    ++samples_;
    const float dt = float(std::clamp(gapMs, 0.0, 50.0)); // ms; slews are frame-rate independent

    ControllerState result;
    ControllerState k;
    if (!enabled_ || set_.mode == InputMode::ControllerOnly) {
        result = pad;
        latched_.reset();
    } else {
        // --- wheel pulses
        const float pulse = std::clamp(active_.keys.wheelPulseMs, 16.f, 250.f);
        for (auto& w : wheels_) {
            if (now < w.phaseEnd) continue;
            if (w.on) { w.on = false; w.phaseEnd = now + Time100ns(std::max(pulse * 0.5f, 10.f) * Ms); }
            else if (w.pending > 0) { w.on = true; --w.pending; w.phaseEnd = now + Time100ns(pulse * Ms); }
        }
        const auto isOn = [&](InputCode c) {
            if (isWheel(c)) return wheels_[c - code::WheelUp].on;
            return (down_[c] || latched_[c]) && !suppressed_[c];
        };
        float l2t = 0, r2t = 0, walk = 1, precision = 1;
        bool radialOn = false, precisionOn = false;
        std::array<std::uint64_t, 4> leftDir{}; // up, down, left, right → press order (0 = off)
        float rkx = 0, rky = 0;
        for (std::size_t i = 0; i < active_.bindings.size(); ++i) {
            const auto& b = active_.bindings[i];
            bool on;
            if (b.behavior == Behavior::Toggle) on = toggled_[i];
            else {
                on = isOn(b.key) && (!b.modifier || down_[b.modifier] || latched_[b.modifier]);
                if (on && !b.modifier) {
                    for (const auto& o : active_.bindings)
                        if (o.key == b.key && o.modifier && (down_[o.modifier] || latched_[o.modifier])) { on = false; break; }
                }
                if (on && b.behavior == Behavior::Repeat && !isWheel(b.key)) {
                    const double hz = std::clamp(b.repeatHz, 1.f, 30.f);
                    const double phase = std::fmod(double(now - pressedAt_[b.key]) / 1e7 * hz, 1.0);
                    on = phase < 0.5;
                }
            }
            if (!on) continue;
            if (const auto bit = buttonBit(b.target)) { k.buttons |= bit; }
            const std::uint64_t ord = isWheel(b.key) ? order_ + 1 : pressOrder_[b.key] + 1;
            switch (b.target) {
            case Target::L2: l2t = std::max(l2t, b.value); break;
            case Target::R2: r2t = std::max(r2t, b.value); break;
            case Target::LeftUp: leftDir[0] = std::max(leftDir[0], ord); break;
            case Target::LeftDown: leftDir[1] = std::max(leftDir[1], ord); break;
            case Target::LeftLeft: leftDir[2] = std::max(leftDir[2], ord); break;
            case Target::LeftRight: leftDir[3] = std::max(leftDir[3], ord); break;
            case Target::RightUp: rky -= b.value; break;
            case Target::RightDown: rky += b.value; break;
            case Target::RightLeft: rkx -= b.value; break;
            case Target::RightRight: rkx += b.value; break;
            case Target::WalkModifier: walk = std::min(walk, b.value); break;
            case Target::PrecisionModifier: precisionOn = true; precision = std::min(precision, b.value < 1.f ? b.value : active_.mouse.precisionMultiplier); break;
            default: break;
            }
            if (b.radial) radialOn = true;
        }
        // --- keyboard left stick: opposite keys, diagonals, walk scale, slew
        const auto resolve = [&](std::uint64_t neg, std::uint64_t pos) -> float {
            if (neg && pos) return active_.keys.opposite == OppositeKeys::Neutral ? 0.f : (pos > neg ? 1.f : -1.f);
            return pos ? 1.f : neg ? -1.f : 0.f;
        };
        float tx = resolve(leftDir[2], leftDir[3]), ty = resolve(leftDir[0], leftDir[1]);
        if (tx != 0 && ty != 0 && active_.keys.diagonal == DiagonalMode::Circular) { tx *= 0.70710678f; ty *= 0.70710678f; }
        tx *= walk; ty *= walk;
        const float rise = active_.keys.riseMs > 0 ? dt / active_.keys.riseMs : 1e10f;
        const float fall = active_.keys.fallMs > 0 ? dt / active_.keys.fallMs : 1e10f;
        const auto slew = [&](float c, float t) {
            if (c * t < 0) { c = approach(c, 0, fall); if (c != 0) return c; } // counter-steer: through centre first
            return std::abs(t) > std::abs(c) ? approach(c, t, rise) : approach(c, t, fall);
        };
        leftX_ = slew(leftX_, tx); leftY_ = slew(leftY_, ty);
        const float trig = active_.keys.triggerRiseMs > 0 ? dt / active_.keys.triggerRiseMs : 1e10f;
        l2_ = l2t <= l2_ ? l2t : approach(l2_, l2t, trig);
        r2_ = r2t <= r2_ ? r2t : approach(r2_, r2t, trig);
        // --- mouse
        const bool ads = active_.adsOnL2 && (l2t > 0 || (set_.mode == InputMode::Hybrid && pad.inputActive && pad.l2 > 25));
        if (radialOn && !mouse_.radialActive()) mouse_.beginRadial();
        if (!radialOn && mouse_.radialActive()) mouse_.endRadial();
        const float scale = (ads ? active_.mouse.adsMultiplier : 1.f) * precision;
        auto m = mouse_.sample(now, scale); // always sampled: keeps the estimator's clock current
        if (m.saturated) ++saturated_;
        float lx = leftX_, ly = leftY_, rx = rkx, ry = rky;
        if (radialOn) { const auto r = mouse_.sampleRadial(); rx += r.x; ry += r.y; }
        else if (active_.mouseTarget == MouseTarget::RightStick) { rx += m.x; ry += m.y; }
        else if (active_.mouseTarget == MouseTarget::LeftStick) { lx += m.x; ly += m.y; }
        if (active_.keys.diagonal == DiagonalMode::Square) { lx = std::clamp(lx, -1.f, 1.f); ly = std::clamp(ly, -1.f, 1.f); }
        else if (const float lm = std::hypot(lx, ly); lm > 1) { lx /= lm; ly /= lm; }
        rx = std::clamp(rx, -1.f, 1.f); ry = std::clamp(ry, -1.f, 1.f);
        k.inputActive = true;
        k.leftX = toAxis(lx); k.leftY = toAxis(ly); k.rightX = toAxis(rx); k.rightY = toAxis(ry);
        k.l2 = std::uint8_t(std::lround(std::clamp(l2_, 0.f, 1.f) * 255));
        k.r2 = std::uint8_t(std::lround(std::clamp(r2_, 0.f, 1.f) * 255));
        latched_.reset();
        result = mergeStates(pad, k, set_.mode, set_.priority);
        stats_.leftX = lx; stats_.leftY = ly; stats_.rightX = rx; stats_.rightY = ry; stats_.l2 = l2_; stats_.r2 = r2_;
        stats_.buttons = k.buttons; stats_.radial = radialOn; stats_.ads = ads; stats_.precision = precisionOn; stats_.carrying = mouse_.carrying();
    }
    if (override_) { result.inputActive = true; result.rightX = toAxis((*override_)[0]); result.rightY = toAxis((*override_)[1]); }
    if (!enabled_ || set_.mode == InputMode::ControllerOnly) {
        stats_.leftX = stats_.leftY = stats_.rightX = stats_.rightY = stats_.l2 = stats_.r2 = 0; stats_.buttons = 0;
        stats_.radial = stats_.ads = stats_.precision = stats_.carrying = false;
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    composeSumUs_ += us; composeMaxUs_ = std::max(composeMaxUs_, us);
    if (!windowStart_) windowStart_ = now;
    if (now - windowStart_ >= 10000000) {
        const double secs = double(now - windowStart_) / 1e7;
        stats_.mouseEventsPerSecond = double(mouseEvents_) / secs; stats_.keyEventsPerSecond = double(keyEvents_) / secs;
        stats_.composeAverageUs = samples_ ? composeSumUs_ / double(samples_) : 0; stats_.composeMaxUs = composeMaxUs_;
        stats_.submitIntervalAverageMs = gaps_ ? gapSumMs_ / double(gaps_) : 0; stats_.submitIntervalMaxMs = gapMaxMs_;
        stats_.saturatedPercent = samples_ ? 100.0 * double(saturated_) / double(samples_) : 0;
        windowStart_ = now; mouseEvents_ = keyEvents_ = saturated_ = samples_ = gaps_ = 0; composeSumUs_ = composeMaxUs_ = gapSumMs_ = gapMaxMs_ = 0;
    }
    ++stats_.submissions;
    stats_.overrideActive = override_.has_value();
    return result;
}

KbmStats KbmEngine::stats() const {
    std::lock_guard lock(mutex_);
    KbmStats s = stats_;
    s.enabled = enabled_; s.mode = set_.mode; s.activeProfile = set_.active; s.profileName = active_.name;
    s.mouseReportIntervalMs = mouse_.reportIntervalMs();
    s.heldInputs = int(down_.count());
    return s;
}

} // namespace veyra::remoteplay::kbm
