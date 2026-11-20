// SPDX-License-Identifier: GPL-3.0-only
// Keyboard/mouse → virtual controller: deterministic tests with synthetic clocks.
// No console, no window, no device: every event and timestamp is generated here.
#include "veyra/remoteplay/kbm/Calibration.h"
#include "veyra/remoteplay/kbm/KbmEngine.h"
#include "veyra/remoteplay/kbm/KbmProfile.h"
#include "veyra/remoteplay/kbm/MouseStick.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace veyra::remoteplay;
using namespace veyra::remoteplay::kbm;

namespace {
using Test = std::pair<const char*, void (*)()>;
std::vector<Test>& tests() { static std::vector<Test> v; return v; }
struct AddTest { AddTest(const char* n, void (*f)()) { tests().emplace_back(n, f); } };
#define TEST(name) void name(); AddTest reg_##name(#name, name); void name()
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)
#define NEAR(a, b, tol) do { const double va_ = (a), vb_ = (b); if (!(std::abs(va_ - vb_) <= (tol))) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #a " = " + std::to_string(va_) + " vs " + std::to_string(vb_)); } while (false)

constexpr Time100ns ms(double v) { return Time100ns(v * 10000.0); }
float ax(std::int16_t v) { return float(v) / 32767.f; }
using B = ControllerState::Button;

// An engine on the On-foot profile with deterministic mouse settings, enabled.
struct Rig {
    KbmEngine e;
    Time100ns now = ms(1000);
    ControllerState last;
    explicit Rig(std::function<void(ProfileSet&)> tweak = {}, InputMode mode = InputMode::KeyboardMouse) {
        auto s = defaultProfiles(); s.mode = mode;
        if (tweak) tweak(s);
        e.setProfiles(s);
        e.setEnabled(true);
    }
    void key(InputCode c, bool down) { e.key(c, down, now); }
    ControllerState tick(double stepMs = 4, const ControllerState& pad = {}) { now += ms(stepMs); last = e.compose(pad, now); return last; }
    void run(double totalMs, double stepMs = 4) { for (double t = 0; t < totalMs; t += stepMs) tick(stepMs); }
};
bool neutral(const ControllerState& s) {
    return s.buttons == 0 && s.leftX == 0 && s.leftY == 0 && s.rightX == 0 && s.rightY == 0 && s.l2 == 0 && s.r2 == 0;
}
MouseConfig linearMouse() {
    MouseConfig m; m.dpi = 800; m.sensitivity = 1; m.antiDeadzone = 0; m.adsMultiplier = 0.5f; m.curve.kind = CurveKind::Linear;
    return m;
}
// Moves the mouse at a constant physical speed (counts/s) with the given polling rate,
// sampling at 250 Hz. Returns the average stick X over the steady-state part.
double steadyStick(double countsPerSecond, double pollHz, MouseConfig cfg, double durationMs = 400, double* lastOut = nullptr) {
    MouseStick m; m.configure(cfg);
    const Time100ns start = ms(5000);
    const double interval = 1000.0 / pollHz;
    double carry = 0, nextEvent = 0, sum = 0; int n = 0;
    for (double t = 0; t <= durationMs; t += 4) {
        while (nextEvent <= t) {
            carry += countsPerSecond * interval / 1000.0;
            const int counts = int(std::floor(carry)); carry -= counts; // integer reports, like real hardware
            m.add(counts, 0, start + ms(nextEvent));
            nextEvent += interval;
        }
        const auto o = m.sample(start + ms(t), 1.f);
        if (t >= 100) { sum += o.x; ++n; }
        if (lastOut) *lastOut = o.x;
    }
    return sum / n;
}
} // namespace

// ---------------------------------------------------------------- names & profiles
TEST(code_names_roundtrip) {
    for (InputCode c = 1; c < code::Max; ++c) {
        const auto name = codeName(c);
        if (name.empty()) continue;
        const auto back = parseCode(name);
        CHECK(back && *back == c);
    }
    CHECK(parseCode("lshift") == code::LShift);
    CHECK(parseCode("VK0x5B") == InputCode(0x5B));
    CHECK(!parseCode("VK0xFF"));
    CHECK(!parseCode("NotAKey"));
    CHECK(parseCode("F10") == InputCode(0x79));
    CHECK(parseTarget("Share") == Target::Create);
}

TEST(default_profiles_are_valid_and_conflict_free) {
    auto s = defaultProfiles();
    CHECK(s.profiles.size() == 4);
    CHECK(sanitize(s).empty());
    for (const auto& p : s.profiles)
        for (const auto& c : findConflicts(p)) CHECK(!c.error);
    // Required PC defaults (proposed, not verified GTA VI mappings).
    const auto& f = s.profiles[0];
    const auto has = [&](InputCode k, Target t) { for (auto& b : f.bindings) if (b.key == k && b.target == t && !b.modifier) return true; return false; };
    CHECK(has('W', Target::LeftUp) && has('S', Target::LeftDown) && has('A', Target::LeftLeft) && has('D', Target::LeftRight));
    CHECK(has(code::MouseRight, Target::L2) && has(code::MouseLeft, Target::R2) && has(code::LShift, Target::Cross));
    CHECK(has(code::Space, Target::Square) && has('R', Target::Circle) && has('E', Target::DpadRight) && has('F', Target::Triangle));
    CHECK(has(code::LCtrl, Target::L3) && has('M', Target::Touchpad) && has(code::Tab, Target::L1) && has(code::Escape, Target::Options));
}

TEST(serialization_roundtrip) {
    auto s = defaultProfiles();
    s.mode = InputMode::KeyboardMouse; s.priority = StickPriority::Controller; s.active = 2;
    s.profiles[1].mouse.curve.kind = CurveKind::Table;
    s.profiles[1].mouse.curve.table = {{0, 0}, {0.15f, 0}, {0.5f, 0.4f}, {1, 1}};
    s.profiles[0].bindings.push_back([] { Binding b; b.key = 'G'; b.modifier = code::LShift; b.target = Target::Create; b.behavior = Behavior::Toggle; return b; }());
    s.profiles[0].bindings.push_back([] { Binding b; b.key = 'V'; b.target = Target::Cross; b.behavior = Behavior::Repeat; b.repeatHz = 12; return b; }());
    const auto text = serialize(s);
    const auto r = parse(text);
    CHECK(r.set);
    CHECK(r.warnings.empty());
    CHECK(r.set->mode == s.mode && r.set->priority == s.priority && r.set->active == 2);
    CHECK(r.set->profiles.size() == s.profiles.size());
    for (std::size_t i = 0; i < s.profiles.size(); ++i) {
        const auto& a = s.profiles[i]; const auto& b = r.set->profiles[i];
        CHECK(a.name == b.name && a.mouseTarget == b.mouseTarget && a.adsOnL2 == b.adsOnL2);
        CHECK(a.bindings == b.bindings);
        CHECK(a.keys == b.keys);
        NEAR(a.mouse.sensitivity, b.mouse.sensitivity, 1e-4); NEAR(a.mouse.antiDeadzone, b.mouse.antiDeadzone, 1e-4);
        CHECK(a.mouse.curve.kind == b.mouse.curve.kind && a.mouse.curve.table.size() == b.mouse.curve.table.size());
    }
    // Single-profile export/import.
    auto one = parseProfile(serializeProfile(s.profiles[2]));
    CHECK(one && one->name == "Driving" && one->bindings == s.profiles[2].bindings);
}

TEST(corrupted_configuration_recovery) {
    CHECK(!parse("").set);
    CHECK(!parse("garbage\n\x01\x02").set);
    CHECK(!parse("format=99\n[profile]\nname=x\n").set);                       // future version refused, not guessed
    CHECK(!parse("format=1\n").set);                                           // no profiles
    CHECK(!parse(std::string(600 * 1024, 'a')).set);                           // size bound
    const auto r = parse("format=1\nactive=7\n[profile]\nname=X\nmouse.sensitivity=9999\nmouse.antiDeadzone=nan\n"
                         "bind=W>LeftUp\nbind=W>Nope\nbind=Nope>Cross\nbind=Q\nmouse.gamma=abc\nweird line\nbind=LShift+LShift>Cross\n");
    CHECK(r.set);
    CHECK(r.warnings.size() >= 5);
    const auto& p = r.set->profiles[0];
    CHECK(p.mouse.sensitivity == 50.f);              // clamped
    CHECK(p.bindings.size() == 1);                    // only the valid one survives
    CHECK(r.set->active == 0);                        // out-of-range active reset
    // An invalid calibration table never reaches the translator.
    const auto t = parse("format=1\n[profile]\nname=T\nmouse.curve=table\nmouse.table=0/0,0.5/0.9,0.4/1\n");
    CHECK(t.set && t.set->profiles[0].mouse.curve.kind == CurveKind::Linear);
}

TEST(profile_field_access) {
    auto p = defaultOnFoot();
    const auto fields = profileFields(p);
    CHECK(fields.size() > 20);
    bool found = false;
    for (auto& [k, v] : fields) if (k == "mouse.sensitivity") { found = true; CHECK(v == "1"); }
    CHECK(found);
    CHECK(setProfileField(p, "mouse.sensitivity", "2.5") && p.mouse.sensitivity == 2.5f);
    CHECK(setProfileField(p, "mouse.antiDeadzone", "5") && p.mouse.antiDeadzone == 0.6f); // clamped
    CHECK(setProfileField(p, "mouse.curve", "power") && p.mouse.curve.kind == CurveKind::Power);
    std::string why;
    CHECK(!setProfileField(p, "mouse.curve", "spline", &why) && !why.empty());
    CHECK(!setProfileField(p, "bind", "W>Cross"));
    CHECK(!setProfileField(p, "mouse.dpi", "abc"));
    auto b = parseBindingText("LCtrl+F>Options:toggle");
    CHECK(b && b->modifier == code::LCtrl && b->key == 'F' && b->target == Target::Options && b->behavior == Behavior::Toggle);
    CHECK(bindingText(*b) == "LCtrl+F>Options:toggle");
    CHECK(!parseBindingText("F>Nowhere"));
}

TEST(conflict_detection) {
    KbmProfile p; p.name = "c";
    Binding a; a.key = 'F'; a.target = Target::Triangle;
    Binding b = a; b.target = Target::Cross;
    Binding c; c.key = 'F'; c.modifier = code::LShift; c.target = Target::Options;
    Binding d; d.key = code::LShift; d.target = Target::Cross;
    p.bindings = {a, b, c, d};
    const auto conflicts = findConflicts(p);
    int errors = 0, warnings = 0;
    for (auto& x : conflicts) (x.error ? errors : warnings)++;
    CHECK(errors == 1);   // F→Triangle and F→Cross
    CHECK(warnings == 1); // LShift is both a modifier and a binding
}

// ---------------------------------------------------------------- keyboard mapping
TEST(wasd_and_diagonals) {
    Rig r;
    r.key('W', true);
    auto s = r.tick();
    CHECK(s.leftY == -32767 && s.leftX == 0);
    r.key('D', true);
    s = r.tick();
    NEAR(ax(s.leftX), 0.7071, 1e-3); NEAR(ax(s.leftY), -0.7071, 1e-3); // circular gate: same speed diagonally
    r.key('A', true); // A pressed after D: last input wins
    s = r.tick();
    NEAR(ax(s.leftX), -0.7071, 1e-3);
    r.key('A', false);
    s = r.tick();
    NEAR(ax(s.leftX), 0.7071, 1e-3); // D still held
    r.key('W', false); r.key('D', false);
    CHECK(neutral(r.tick()));
}

TEST(opposite_keys_neutral_mode_and_square_diagonal) {
    Rig r([](ProfileSet& s) { s.profiles[0].keys.opposite = OppositeKeys::Neutral; s.profiles[0].keys.diagonal = DiagonalMode::Square; });
    r.key('A', true); r.key('D', true);
    CHECK(r.tick().leftX == 0);
    r.key('A', false); r.key('W', true);
    auto s = r.tick();
    CHECK(s.leftX == 32767 && s.leftY == -32767);
}

TEST(walk_modifier) {
    Rig r;
    r.key('W', true); r.key(code::LAlt, true);
    NEAR(ax(r.tick().leftY), -0.5, 1e-3);
}

TEST(simultaneous_inputs) {
    Rig r;
    for (InputCode c : {InputCode(code::MouseLeft), InputCode(code::MouseRight), InputCode(code::LShift), InputCode(code::Space), InputCode('R'), InputCode('W')}) r.key(c, true);
    const auto s = r.tick();
    CHECK(s.r2 == 255 && s.l2 == 255);
    CHECK((s.buttons & B::Cross) && (s.buttons & B::Square) && (s.buttons & B::Circle));
    CHECK(s.leftY == -32767);
}

TEST(short_tap_reaches_one_submission) {
    Rig r;
    r.tick();
    r.key(code::Space, true); r.key(code::Space, false); // shorter than one 4 ms submission
    CHECK(r.tick().buttons & B::Square);
    CHECK(!(r.tick().buttons & B::Square));
}

TEST(auto_repeat_is_not_a_new_press) {
    Rig r([](ProfileSet& s) { Binding b; b.key = 'G'; b.target = Target::Create; b.behavior = Behavior::Toggle; s.profiles[0].bindings.push_back(b); });
    r.key('G', true); r.key('G', true); r.key('G', true); // keyboard auto-repeat
    CHECK(r.tick().buttons & B::Share);
    r.key('G', false);
    CHECK(r.tick().buttons & B::Share); // toggled on
    r.key('G', true); r.key('G', false);
    CHECK(!(r.tick().buttons & B::Share));
}

TEST(repeat_behaviour_alternates_slowly_enough) {
    Rig r([](ProfileSet& s) { for (auto& b : s.profiles[0].bindings) if (b.key == code::LShift) { b.behavior = Behavior::Repeat; b.repeatHz = 10; } });
    r.key(code::LShift, true);
    int edges = 0; bool prev = false; int run = 0, minRun = 1000;
    for (int i = 0; i < 250; ++i) { // 1 s
        const bool on = r.tick().buttons & B::Cross;
        if (on != prev) { ++edges; if (i) minRun = std::min(minRun, run); run = 0; }
        ++run; prev = on;
    }
    CHECK(edges >= 18 && edges <= 22); // ~10 presses → ~20 edges
    CHECK(minRun >= 2);                // every phase lasts ≥ 2 submissions
}

TEST(modifier_binding_shadows_plain) {
    Rig r([](ProfileSet& s) { Binding b; b.key = 'F'; b.modifier = code::LCtrl; b.target = Target::Options; s.profiles[0].bindings.push_back(b); });
    r.key('F', true);
    auto s = r.tick();
    CHECK((s.buttons & B::Triangle) && !(s.buttons & B::Options));
    r.key('F', false); r.tick();
    r.key(code::LCtrl, true); r.key('F', true);
    s = r.tick();
    CHECK((s.buttons & B::Options) && !(s.buttons & B::Triangle));
    CHECK(s.buttons & B::L3); // LCtrl itself still crouches (reported as a warning by findConflicts)
}

TEST(mouse_side_buttons_and_wheel_pulses) {
    Rig r([](ProfileSet& s) { s.active = 1; }); // Combat: X1 → R1, X2 → precision
    r.key(code::MouseX1, true);
    CHECK(r.tick().buttons & B::R1);
    r.key(code::MouseX1, false);
    r.e.wheel(code::WheelUp, 2, r.now);
    int presses = 0; bool prev = false; int longest = 0, run = 0;
    for (int i = 0; i < 60; ++i) {
        const bool on = r.tick().buttons & B::Left;
        if (on && !prev) ++presses;
        run = on ? run + 1 : 0; longest = std::max(longest, run);
        prev = on;
    }
    CHECK(presses == 2);       // two notches → two separate presses, never merged
    CHECK(longest >= 4);       // each held long enough to register (48 ms at 4 ms)
    r.e.wheel(code::WheelDown, 100, r.now);
    int down = 0; prev = false;
    for (int i = 0; i < 400; ++i) { const bool on = r.tick().buttons & B::Right; if (on && !prev) ++down; prev = on; }
    CHECK(down == 8);          // bounded: a fast spin is not an unbounded queue
}

TEST(focus_loss_releases_everything) {
    Rig r([](ProfileSet& s) { Binding b; b.key = 'G'; b.target = Target::Create; b.behavior = Behavior::Toggle; s.profiles[0].bindings.push_back(b); });
    r.key('W', true); r.key(code::MouseLeft, true); r.key('G', true); r.key('G', false);
    r.e.mouseMove(300, 0, r.now);
    CHECK(!neutral(r.tick()));
    r.e.setEnabled(false);
    CHECK(neutral(r.tick()));
    r.e.setEnabled(true);
    CHECK(neutral(r.tick()));                 // toggles and held keys do not come back by themselves
    r.key(code::MouseLeft, false);            // late key-up after refocus is harmless
    CHECK(neutral(r.tick()));
    r.key('W', true);                          // a fresh press works
    CHECK(r.tick().leftY == -32767);
}

TEST(disabled_engine_ignores_input) {
    Rig r; r.e.setEnabled(false);
    r.key('W', true); r.e.mouseMove(500, 500, r.now); r.e.wheel(code::WheelUp, 1, r.now);
    ControllerState pad; pad.inputActive = true; pad.buttons = B::Cross; pad.rightX = 1000;
    const auto s = r.tick(4, pad);
    CHECK(s == pad); // pad passes through untouched
}

TEST(profile_switch_releases_and_suppresses_held_keys) {
    Rig r;
    r.key('W', true);
    CHECK(r.tick().leftY == -32767);
    r.key(code::F1 + 2, true);                // F3 → Driving, where W is throttle
    auto s = r.tick();
    CHECK(r.e.stats().profileName == "Driving");
    CHECK(neutral(s));                        // W still held but suppressed: no surprise throttle
    r.key(code::F1 + 2, false);
    r.key('W', false); r.key('W', true);
    r.run(100);
    CHECK(r.last.r2 == 255);
    r.e.selectProfile(0);
    CHECK(neutral(r.tick()));
}

TEST(invalid_codes_ignored) {
    Rig r;
    r.key(0, true); r.key(0xFF, true); r.key(0x200, true); r.key(code::WheelUp, true);
    CHECK(neutral(r.tick()));
}

// ---------------------------------------------------------------- mouse translation
TEST(polling_rate_independence) {
    const auto cfg = linearMouse();
    const double speed = 2000; // counts/s → 0.5 of full scale at 800 dpi, sensitivity 1
    std::vector<double> outs;
    for (double hz : {125.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) outs.push_back(steadyStick(speed, hz, cfg));
    for (double o : outs) NEAR(o, 0.5, 0.03);
    // Per-sample jitter matters too: a 125 Hz mouse sampled at 250 Hz must not alternate 0 / 2x.
    MouseStick m; m.configure(cfg);
    double lo = 9, hi = -9;
    for (int i = 0; i < 200; ++i) {
        const double t = i * 4.0;
        if (i % 2 == 0) m.add(int(speed * 0.008), 0, ms(1000 + t));
        const auto o = m.sample(ms(1000 + t + 1), 1);
        if (i > 20) { lo = std::min(lo, double(o.x)); hi = std::max(hi, double(o.x)); }
    }
    CHECK(hi - lo < 0.06);
}

TEST(high_dpi_normalization) {
    auto a = linearMouse(); a.dpi = 800;
    auto b = linearMouse(); b.dpi = 26000;
    const double inchesPerSecond = 3;
    NEAR(steadyStick(inchesPerSecond * 800, 1000, a), steadyStick(inchesPerSecond * 26000, 1000, b), 0.01);
    NEAR(steadyStick(inchesPerSecond * 26000, 8000, b), 0.6, 0.03);
}

TEST(immediate_stop_and_no_drift) {
    MouseStick m; m.configure(linearMouse());
    Time100ns t = ms(1000);
    for (int i = 0; i < 100; ++i) { m.add(2, 1, t); t += ms(1); if (i % 4 == 3) m.sample(t, 1); }
    CHECK(m.sample(t, 1).x > 0);
    // 1000 Hz mouse: zero within one 4 ms submission after the last report.
    const auto o = m.sample(t + ms(4), 1);
    CHECK(o.x == 0 && o.y == 0);
    for (int i = 0; i < 100; ++i) { const auto z = m.sample(t + ms(8 + 4 * i), 1); CHECK(z.x == 0 && z.y == 0); }
    // Exactly zero when nothing ever moved.
    MouseStick idle; idle.configure(linearMouse());
    for (int i = 0; i < 10; ++i) CHECK(idle.sample(ms(100 + 4 * i), 1).x == 0);
}

TEST(stop_latency_bounded_for_slow_polling) {
    // 125 Hz mouse: allowed to hold ~1.5 report intervals, never longer.
    MouseStick m; m.configure(linearMouse());
    Time100ns t = ms(1000);
    for (int i = 0; i < 40; ++i) { m.add(16, 0, t); t += ms(8); }
    const Time100ns last = t - ms(8);
    double zeroAt = -1;
    for (double d = 0; d < 40; d += 1) if (m.sample(last + ms(d), 1).x == 0) { zeroAt = d; break; }
    CHECK(zeroAt > 0 && zeroAt <= 13.5);
}

TEST(anti_deadzone_and_small_corrections) {
    auto cfg = linearMouse(); cfg.antiDeadzone = 0.2f;
    const auto tiny = shapeStick(0.01f, 0, cfg);
    CHECK(tiny.x >= 0.2f && tiny.x < 0.21f);    // smallest motion escapes the game's dead zone
    const auto none = shapeStick(0, 0, cfg);
    CHECK(none.x == 0 && none.y == 0);          // but rest stays exactly at rest
    // A single count at 800 dpi produces a deliberate, small, non-zero deflection.
    MouseStick m; m.configure(cfg);
    m.add(1, 0, ms(1000));
    const auto o = m.sample(ms(1001), 1);
    CHECK(o.x > 0.2f && o.x <= 0.251f);
}

TEST(axial_deadzone_keeps_axes_independent) {
    auto cfg = linearMouse(); cfg.antiDeadzone = 0.2f; cfg.deadzoneShape = DeadzoneShape::Axial;
    const auto o = shapeStick(0.5f, 0.0f, cfg);
    CHECK(o.y == 0); NEAR(o.x, 0.2 + 0.8 * 0.5, 1e-4);
}

TEST(response_curves_invert_game_curve) {
    MouseConfig cfg = linearMouse(); cfg.antiDeadzone = 0.1f;
    cfg.curve.kind = CurveKind::Power; cfg.curve.gamma = 2.0f;
    // Game: rate = ((s - dz)/(1 - dz))^2. Requested rate 0.25 → s = dz + (1-dz)·0.5.
    const auto o = shapeStick(0.25f, 0, cfg);
    NEAR(o.x, 0.1 + 0.9 * 0.5, 1e-4);
    cfg.curve.kind = CurveKind::Table;
    cfg.curve.table = {{0, 0}, {0.1f, 0}, {0.5f, 0.3f}, {0.8f, 0.8f}, {1, 1}};
    CHECK(cfg.curve.valid());
    NEAR(shapeStick(0.3f, 0, cfg).x, 0.5, 1e-4);
    NEAR(shapeStick(0.55f, 0, cfg).x, 0.65, 1e-4);
    CHECK(shapeStick(0.001f, 0, cfg).x >= 0.1f); // implicit anti-deadzone from the table
}

TEST(diagonal_response_is_predictable) {
    auto cfg = linearMouse(); cfg.antiDeadzone = 0.15f;
    const auto straight = shapeStick(0.4f, 0, cfg);
    const float d = 0.4f / std::sqrt(2.f);
    const auto diag = shapeStick(d, d, cfg);
    NEAR(std::hypot(diag.x, diag.y), straight.x, 1e-4); // same speed in every direction
    NEAR(diag.x, diag.y, 1e-6);
    cfg.diagonal = DiagonalMode::Square;
    const auto sq = shapeStick(d, d, cfg);
    NEAR(sq.x, straight.x, 1e-4); NEAR(sq.y, straight.x, 1e-4);
}

TEST(saturation_clamps_to_ceiling) {
    auto cfg = linearMouse(); cfg.maxOutput = 0.9f;
    const auto o = shapeStick(5.f, 0, cfg);
    NEAR(o.x, 0.9, 1e-5); CHECK(o.saturated);
    CHECK(!shapeStick(0.5f, 0, cfg).saturated);
    CHECK(toAxis(1.f) == 32767 && toAxis(-1.f) == -32767 && toAxis(0.f) == 0 && toAxis(NAN) == 0);
}

TEST(sensitivity_axes_and_invert) {
    auto cfg = linearMouse(); cfg.sensitivityY = 0.5f; cfg.invertY = true;
    MouseStick m; m.configure(cfg);
    Time100ns t = ms(1000);
    for (int i = 0; i < 50; ++i) { m.add(2, 2, t); t += ms(1); }
    const auto o = m.sample(t, 1);
    NEAR(o.y, -o.x * 0.5, 0.02);
}

TEST(ads_transition_scales_mouse) {
    Rig r([](ProfileSet& s) { s.profiles[0].mouse = linearMouse(); s.profiles[0].mouse.adsMultiplier = 0.5f; });
    auto move = [&](int n) { for (int i = 0; i < n; ++i) { for (int k = 0; k < 4; ++k) r.e.mouseMove(1, 0, r.now + ms(k)); r.tick(); } };
    move(30);
    const float hip = ax(r.last.rightX);
    r.key(code::MouseRight, true); // aim: ADS multiplier from the very next submission
    move(1);
    const float ads = ax(r.last.rightX);
    CHECK(r.last.l2 == 255);
    NEAR(ads, hip * 0.5, 0.03);
    r.key(code::MouseRight, false);
    move(1);
    NEAR(ax(r.last.rightX), hip, 0.03);
    CHECK(r.e.stats().ads == false);
}

TEST(precision_modifier) {
    Rig r([](ProfileSet& s) { s.active = 1; s.profiles[1].mouse = linearMouse(); });
    auto move = [&] { for (int i = 0; i < 20; ++i) { for (int k = 0; k < 4; ++k) r.e.mouseMove(1, 0, r.now + ms(k)); r.tick(); } return ax(r.last.rightX); };
    const float base = move();
    r.key(code::MouseX2, true);
    NEAR(move(), base * 0.4, 0.03);
}

TEST(zero_smoothing_is_exact_and_smoothing_settles) {
    auto raw = linearMouse();
    auto smooth = linearMouse(); smooth.smoothingMs = 20;
    MouseStick a, b; a.configure(raw); b.configure(smooth);
    Time100ns t = ms(1000);
    // Step input: zero smoothing reaches the target in the first sample after the window fills.
    for (int i = 0; i < 8; ++i) { a.add(2, 0, t); b.add(2, 0, t); t += ms(1); }
    const auto ra = a.sample(t, 1), rb = b.sample(t, 1);
    NEAR(ra.x, 0.5, 0.02);
    CHECK(rb.x < ra.x);         // smoothing lags...
    double last = 1;
    for (int i = 1; i < 60; ++i) last = b.sample(t + ms(4 * i), 1).x;
    CHECK(last == 0);           // ...but returns to exact zero (no residual drift)
}

TEST(overflow_carry_is_bounded_and_reversal_safe) {
    auto cfg = linearMouse(); cfg.overflowCarryMs = 100;
    MouseStick m; m.configure(cfg);
    Time100ns t = ms(1000);
    for (int i = 0; i < 20; ++i) { m.add(20, 0, t); t += ms(1); if (i % 4 == 3) m.sample(t, 1); } // 5x the ceiling
    int maxed = 0;
    for (int i = 0; i < 100; ++i) { const auto o = m.sample(t + ms(4 * (i + 1)), 1); if (o.x >= 0.999f) ++maxed; else break; }
    CHECK(maxed > 3 && maxed <= 26); // keeps turning at the ceiling for ≤ 100 ms, then stops
    CHECK(m.sample(t + ms(500), 1).x == 0);
    // Reversal drops the reservoir immediately.
    MouseStick r; r.configure(cfg); t = ms(5000);
    for (int i = 0; i < 20; ++i) { r.add(20, 0, t); t += ms(1); if (i % 4 == 3) r.sample(t, 1); }
    for (int i = 0; i < 4; ++i) { r.add(-2, 0, t); t += ms(1); }
    CHECK(r.sample(t, 1).x < 0);
}

TEST(radial_menu_positions_stick_without_turning_camera) {
    Rig r([](ProfileSet& s) { s.profiles[0].mouse = linearMouse(); });
    r.key(code::Tab, true);
    for (int i = 0; i < 10; ++i) { r.e.mouseMove(0, -40, r.now); r.tick(); } // flick up
    auto s = r.last;
    CHECK(s.buttons & B::L1);
    CHECK(s.rightY == -32767 && s.rightX == 0); // full deflection toward the slot
    for (int i = 0; i < 10; ++i) r.tick();
    CHECK(r.last.rightY == -32767);            // selection holds while the wheel is open
    r.key(code::Tab, false);
    s = r.tick();
    CHECK(!(s.buttons & B::L1) && s.rightX == 0 && s.rightY == 0); // closes cleanly, no camera jerk
    r.run(40);
    CHECK(neutral(r.last));
}

TEST(keyboard_slew_is_frame_rate_independent) {
    const auto run = [](double step) {
        Rig r([](ProfileSet& s) { s.active = 2; }); // Driving: rise 110 ms, trigger 60 ms
        r.tick(step); // the owner composes continuously; the press lands between submissions
        r.key('D', true); r.key('W', true);
        for (double t = 0; t < 56; t += step) r.tick(step);
        return std::pair{ax(r.last.leftX), float(r.last.r2)};
    };
    const auto a = run(4), b = run(8), c = run(2);
    NEAR(a.first, b.first, 0.02); NEAR(a.first, c.first, 0.02);
    NEAR(a.first, 0.51, 0.05);   // 56 ms of a 110 ms rise
    NEAR(a.second, 238, 4);      // ≈ 56/60 of full throttle
    NEAR(b.second, a.second, 4);
}

TEST(counter_steer_returns_through_centre) {
    Rig r([](ProfileSet& s) { s.active = 2; });
    r.key('D', true); r.run(200);
    CHECK(r.last.leftX == 32767);
    r.key('D', false); r.key('A', true);
    r.run(40);
    const float x = ax(r.last.leftX);
    CHECK(x < 0.5f); // falling at the 70 ms return rate, not snapping to -1
    r.run(400);
    CHECK(r.last.leftX == -32767);
    r.key('A', false); r.run(200);
    CHECK(r.last.leftX == 0);
}

// ---------------------------------------------------------------- hybrid / devices
TEST(hybrid_merge_rules) {
    ControllerState pad; pad.inputActive = true; pad.buttons = B::Cross; pad.rightX = 3000; pad.leftY = 30000; pad.l2 = 100;
    ControllerState k; k.inputActive = true; k.buttons = B::Square; k.rightX = 20000; k.leftY = 0; k.r2 = 255;
    auto m = mergeStates(pad, k, InputMode::Hybrid, StickPriority::Larger);
    CHECK(m.buttons == (B::Cross | B::Square) && m.l2 == 100 && m.r2 == 255);
    CHECK(m.rightX == 20000);   // pad resting drift (0.09) never fights the mouse
    CHECK(m.leftY == 30000);    // physically deflected pad stick wins
    m = mergeStates(pad, k, InputMode::Hybrid, StickPriority::KeyboardMouse);
    CHECK(m.rightX == 20000 && m.leftY == 30000); // KBM preferred only when KBM deflects
    pad.rightX = 25000;
    CHECK(mergeStates(pad, k, InputMode::Hybrid, StickPriority::Controller).rightX == 25000);
    CHECK(mergeStates(pad, k, InputMode::KeyboardMouse, StickPriority::Larger) == k);
    CHECK(mergeStates(pad, k, InputMode::ControllerOnly, StickPriority::Larger) == pad);
    pad.inputActive = false; // pad lost / unfocused: keyboard state alone, no stale pad bits
    CHECK(mergeStates(pad, k, InputMode::Hybrid, StickPriority::Larger) == k);
}

TEST(hybrid_preserves_pad_motion_and_touch) {
    Rig r({}, InputMode::Hybrid);
    ControllerState pad; pad.inputActive = true; pad.motionValid = true; pad.gyro = {1, 2, 3}; pad.touches[0].id = 4; pad.touches[0].x = 100;
    r.key('W', true);
    const auto s = r.tick(4, pad);
    CHECK(s.motionValid && s.gyro == pad.gyro && s.touches == pad.touches && s.leftY == -32767);
    r.e.setMode(InputMode::ControllerOnly, StickPriority::Larger);
    CHECK(r.tick(4, pad) == pad);
}

TEST(pad_l2_triggers_ads_in_hybrid) {
    Rig r([](ProfileSet& s) { s.profiles[0].mouse = linearMouse(); }, InputMode::Hybrid);
    ControllerState pad; pad.inputActive = true; pad.l2 = 200;
    for (int k = 0; k < 4; ++k) r.e.mouseMove(1, 0, r.now + ms(k));
    r.tick(4, pad);
    CHECK(r.e.stats().ads);
}

TEST(calibration_override_drives_right_stick) {
    Rig r;
    r.e.setEnabled(false); // the player is clicking the calibration UI, capture is off
    r.e.setRightStickOverride(std::array<float, 2>{0.3f, 0});
    ControllerState pad;
    auto s = r.tick(4, pad);
    NEAR(ax(s.rightX), 0.3, 1e-4); CHECK(s.inputActive);
    r.e.setRightStickOverride(std::nullopt);
    CHECK(r.tick(4, pad) == pad);
}

// ---------------------------------------------------------------- stuck-input fuzz
TEST(fuzz_never_leaves_inputs_stuck) {
    std::mt19937 rng(1234);
    Rig r;
    std::vector<InputCode> keys;
    for (const auto& p : defaultProfiles().profiles) for (const auto& b : p.bindings) if (!isWheel(b.key)) keys.push_back(b.key);
    std::vector<bool> held(code::Max, false);
    for (int step = 0; step < 20000; ++step) {
        const int action = int(rng() % 100);
        if (action < 60) { const auto k = keys[rng() % keys.size()]; held[k] = !held[k]; r.key(k, held[k]); }
        else if (action < 80) r.e.mouseMove(int(rng() % 81) - 40, int(rng() % 81) - 40, r.now);
        else if (action < 83) r.e.wheel(InputCode(code::WheelUp + rng() % 2), 1, r.now);
        else if (action < 85) r.e.setEnabled(rng() % 2);
        else if (action < 86) r.e.selectProfile(int(rng() % 4));
        r.tick(double(1 + rng() % 8));
    }
    // Physically let go of everything: the output must settle to neutral.
    for (InputCode k = 0; k < code::Max; ++k) if (held[k]) r.key(k, false);
    r.e.setEnabled(true);
    r.run(1000);
    if (!neutral(r.last)) {
        // Toggle bindings are allowed to stay latched by design; only the default
        // profiles are used here and none of them has a toggle.
        throw std::runtime_error("stuck output after releasing all inputs");
    }
}

TEST(concurrent_producer_consumer) {
    KbmEngine e; e.setEnabled(true);
    std::atomic<bool> stop{false};
    std::atomic<Time100ns> clock{ms(1000)};
    std::thread producer([&] {
        std::mt19937 rng(7); int i = 0;
        while (!stop.load()) {
            const auto t = clock.load();
            e.mouseMove(int(rng() % 11) - 5, int(rng() % 11) - 5, t);
            if (++i % 50 == 0) e.key('W', i % 100 == 0, t);
            if (i % 5000 == 0) e.selectProfile(i / 5000 % 4);
        }
    });
    std::thread ui([&] { while (!stop.load()) { (void)e.stats(); e.setEnabled(true); std::this_thread::yield(); } });
    for (int i = 0; i < 50000; ++i) { clock += ms(0.2); (void)e.compose({}, clock.load()); }
    stop = true; producer.join(); ui.join();
    e.setEnabled(false);
    CHECK(neutral(e.compose({}, clock.load() + ms(4))));
}

// ---------------------------------------------------------------- timing
TEST(processing_time_per_submission) {
    // 8 kHz mouse + held keys, 250 Hz submissions: measure real compose() cost.
    Rig r;
    r.key('W', true); r.key('D', true); r.key(code::MouseLeft, true);
    using clk = std::chrono::steady_clock;
    double worst = 0, total = 0; const int n = 2500;
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 32; ++k) r.e.mouseMove(3, -1, r.now + ms(k * 0.125));
        const auto a = clk::now();
        r.tick();
        const double us = std::chrono::duration<double, std::micro>(clk::now() - a).count();
        worst = std::max(worst, us); total += us;
    }
    const auto st = r.e.stats();
    std::printf("    compose avg %.2f us, worst %.2f us (engine-reported avg %.2f us), submit interval avg %.2f ms\n", total / n, worst, st.composeAverageUs, st.submitIntervalAverageMs);
    CHECK(total / n < 1000.0); // engineering target: < 1 ms per submission
    NEAR(st.submitIntervalAverageMs, 4.0, 0.01);
    NEAR(st.mouseEventsPerSecond, 8000, 100);
}

// ---------------------------------------------------------------- calibration
TEST(deadzone_sweep_with_reaction_allowance) {
    DeadzoneSweep s;
    s.start(ms(0), 0, 0.6f, 0.05f);
    // True dead zone 0.15 reached at 3 s; the player reacts 220 ms later.
    const float est = s.mark(ms(3000 + 220));
    NEAR(est, 0.15, 0.002);
    CHECK(!s.running());
}

TEST(turn_rate_probe_and_curve_fit) {
    // Synthetic game: dead zone 0.15, rate = lin^2, saturates (no further gain) above 0.85, 180 °/s max.
    const auto game = [](float d) {
        const float lin = std::clamp((std::min(d, 0.85f) - 0.15f) / (0.85f - 0.15f), 0.f, 1.f);
        return lin * lin; // normalized
    };
    std::vector<TurnRateProbe::Sample> samples;
    for (float d : {0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f}) {
        TurnRateProbe p; p.begin(d, 0);
        const double secs = 2.0 / std::max(1e-3f, game(d)); // 180 °/s max → 2 s per revolution
        for (int i = 0; i < 3; ++i) p.mark(Time100ns(i * secs * 1e7));
        auto r = p.result(); CHECK(r); samples.push_back(*r);
    }
    const auto rep = buildCalibration(0.15f, samples);
    CHECK(rep.curve.kind == CurveKind::Table && rep.curve.valid());
    CHECK(rep.saturation && std::abs(*rep.saturation - 0.9f) < 1e-3f);
    NEAR(rep.maxDegreesPerSecond, 180, 1);
    CHECK(!rep.timeRamp);
    // Linearization: requested rate → table deflection → game rate tracks the request.
    MouseConfig cfg; cfg.curve = rep.curve; cfg.antiDeadzone = 0;
    for (float want : {0.1f, 0.3f, 0.5f, 0.8f}) NEAR(game(cfg.curve.deflectionFor(want, 0)), want, 0.06);
    KbmProfile p = defaultCombat();
    applyCalibration(p, rep);
    CHECK(p.mouse.curve.kind == CurveKind::Table && p.mouse.maxOutput <= 0.9f);
}

TEST(turn_rate_probe_detects_time_ramp_and_ignores_double_clicks) {
    TurnRateProbe p; p.begin(1.0f, 0);
    p.mark(ms(0)); p.mark(ms(50)); // double click ignored
    p.mark(ms(2600)); p.mark(ms(4600)); p.mark(ms(6600));
    const auto r = p.result();
    CHECK(r && r->revolutions == 3);
    NEAR(r->secondsPerRevolution, 2.0, 1e-3);
    const auto rep = buildCalibration(0.1f, {*r});
    CHECK(rep.timeRamp);
    CHECK(rep.curve.kind == CurveKind::Linear); // one deflection is not enough for a table
}

TEST(linearity_verdicts) {
    CHECK(linearity(4000, 4100).linear);
    CHECK(!linearity(4000, 6000).linear);
    CHECK(!linearity(4000, 2500).linear);
    CHECK(!linearity(10, 4000).linear);
}

int main(int argc, char** argv) {
    int failed = 0, ran = 0;
    for (const auto& [name, fn] : tests()) {
        if (argc > 1 && std::string(argv[1]) != name) continue;
        ++ran;
        try { fn(); std::cout << "[pass] " << name << "\n"; }
        catch (const std::exception& e) { ++failed; std::cout << "[FAIL] " << name << ": " << e.what() << "\n"; }
    }
    std::cout << ran - failed << "/" << ran << " passed\n";
    return failed ? 1 : 0;
}
