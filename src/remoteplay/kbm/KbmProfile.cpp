// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/kbm/KbmProfile.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>

namespace veyra::remoteplay::kbm {
namespace {
using namespace code;
Binding bind(InputCode key, Target t, Behavior b = Behavior::Hold, float value = 1.f) { Binding out; out.key = key; out.target = t; out.behavior = b; out.value = value; return out; }
Binding radial(InputCode key, Target t) { auto b = bind(key, t); b.radial = true; return b; }
void addProfileKeys(KbmProfile& p) {
    // F1..F4 switch between the four default profiles in every profile.
    for (int i = 0; i < 4; ++i) p.bindings.push_back(bind(InputCode(F1 + i), Target::SwitchProfile, Behavior::Hold, float(i)));
}
void addMovement(KbmProfile& p) {
    p.bindings.push_back(bind(W, Target::LeftUp));
    p.bindings.push_back(bind(S, Target::LeftDown));
    p.bindings.push_back(bind(A, Target::LeftLeft));
    p.bindings.push_back(bind(D, Target::LeftRight));
    p.bindings.push_back(bind(LAlt, Target::WalkModifier, Behavior::Hold, 0.5f));
}
// Shared on-foot layout (GTA V PlayStation layout as the assumed reference).
void addOnFootButtons(KbmProfile& p) {
    p.bindings.push_back(bind(MouseRight, Target::L2));        // aim
    p.bindings.push_back(bind(MouseLeft, Target::R2));         // shoot
    p.bindings.push_back(bind(LShift, Target::Cross));         // sprint
    p.bindings.push_back(bind(Space, Target::Square));         // jump
    p.bindings.push_back(bind(R, Target::Circle));             // reload
    p.bindings.push_back(bind(E, Target::DpadRight));          // interact / pick up (assumed)
    p.bindings.push_back(bind(F, Target::Triangle));           // enter / exit vehicle
    p.bindings.push_back(bind(LCtrl, Target::L3));             // crouch / stealth
    p.bindings.push_back(bind(M, Target::Touchpad));           // map
    p.bindings.push_back(radial(Tab, Target::L1));             // weapon wheel: mouse positions the stick
    p.bindings.push_back(bind(Escape, Target::Options));       // pause
    p.bindings.push_back(bind(Q, Target::R1));                 // cover
    p.bindings.push_back(bind(C, Target::R3));                 // look behind
    p.bindings.push_back(bind(MouseMiddle, Target::R3));
    p.bindings.push_back(bind(T, Target::DpadUp));             // phone
    p.bindings.push_back(bind(B, Target::DpadDown));
    p.bindings.push_back(bind(WheelUp, Target::DpadLeft));     // weapon / item cycle (assumed)
    p.bindings.push_back(bind(WheelDown, Target::DpadRight));
    p.bindings.push_back(bind(Backspace, Target::Create));
    p.bindings.push_back(bind(InputCode(F1 + 9), Target::PS)); // F10: PS button, deliberately out of the way
}
} // namespace

KbmProfile defaultOnFoot() {
    KbmProfile p; p.name = "On foot";
    p.mouse.sensitivity = 1.0f; p.mouse.adsMultiplier = 0.6f; p.mouse.antiDeadzone = 0.12f;
    addMovement(p); addOnFootButtons(p); addProfileKeys(p);
    return p;
}
KbmProfile defaultCombat() {
    KbmProfile p = defaultOnFoot(); p.name = "Combat";
    p.mouse.adsMultiplier = 0.5f; p.mouse.smoothingMs = 0; p.mouse.overflowCarryMs = 0;
    p.bindings.push_back(bind(MouseX2, Target::PrecisionModifier, Behavior::Hold, 0.4f));
    p.bindings.push_back(bind(MouseX1, Target::R1));
    return p;
}
KbmProfile defaultDriving() {
    KbmProfile p; p.name = "Driving";
    p.mouse.sensitivity = 0.7f; p.mouse.adsMultiplier = 1.0f; p.adsOnL2 = false; // L2 is the brake here
    p.keys.riseMs = 110; p.keys.fallMs = 70; p.keys.triggerRiseMs = 60;             // gradual steering / throttle
    p.bindings.push_back(bind(W, Target::R2));          // throttle
    p.bindings.push_back(bind(S, Target::L2));          // brake / reverse
    p.bindings.push_back(bind(A, Target::LeftLeft));
    p.bindings.push_back(bind(D, Target::LeftRight));
    p.bindings.push_back(bind(Up, Target::LeftUp));     // lean / pitch (bikes, aircraft)
    p.bindings.push_back(bind(Down, Target::LeftDown));
    p.bindings.push_back(bind(LAlt, Target::WalkModifier, Behavior::Hold, 0.4f)); // gentle steering
    p.bindings.push_back(bind(Space, Target::R1));      // handbrake
    p.bindings.push_back(bind(F, Target::Triangle));    // exit vehicle
    p.bindings.push_back(bind(E, Target::L3));          // horn
    p.bindings.push_back(bind(C, Target::R3));          // look behind
    p.bindings.push_back(bind(MouseRight, Target::L1)); // drive-by aim (assumed)
    p.bindings.push_back(bind(MouseLeft, Target::R1));
    p.bindings.push_back(bind(Q, Target::DpadLeft));    // radio (assumed)
    p.bindings.push_back(bind(H, Target::DpadRight));
    p.bindings.push_back(bind(M, Target::Touchpad));
    p.bindings.push_back(radial(Tab, Target::L1));
    p.bindings.push_back(bind(Escape, Target::Options));
    p.bindings.push_back(bind(T, Target::DpadUp));
    p.bindings.push_back(bind(LShift, Target::Cross));
    p.bindings.push_back(bind(LCtrl, Target::Square));
    p.bindings.push_back(bind(Backspace, Target::Create));
    addProfileKeys(p);
    return p;
}
KbmProfile defaultMenus() {
    KbmProfile p; p.name = "Menus";
    p.mouseTarget = MouseTarget::LeftStick; // controller-style cursor (map, menus)
    p.mouse.sensitivity = 1.6f; p.mouse.antiDeadzone = 0.08f; p.adsOnL2 = false;
    p.bindings.push_back(bind(W, Target::DpadUp)); p.bindings.push_back(bind(S, Target::DpadDown));
    p.bindings.push_back(bind(A, Target::DpadLeft)); p.bindings.push_back(bind(D, Target::DpadRight));
    p.bindings.push_back(bind(Up, Target::DpadUp)); p.bindings.push_back(bind(Down, Target::DpadDown));
    p.bindings.push_back(bind(Left, Target::DpadLeft)); p.bindings.push_back(bind(Right, Target::DpadRight));
    p.bindings.push_back(bind(WheelUp, Target::DpadUp)); p.bindings.push_back(bind(WheelDown, Target::DpadDown));
    p.bindings.push_back(bind(Enter, Target::Cross)); p.bindings.push_back(bind(Space, Target::Cross));
    p.bindings.push_back(bind(MouseLeft, Target::Cross));
    p.bindings.push_back(bind(Escape, Target::Circle)); p.bindings.push_back(bind(Backspace, Target::Circle));
    p.bindings.push_back(bind(MouseRight, Target::Circle));
    p.bindings.push_back(bind(X, Target::Square)); p.bindings.push_back(bind(Tab, Target::Triangle));
    p.bindings.push_back(bind(Q, Target::L1)); p.bindings.push_back(bind(E, Target::R1));
    p.bindings.push_back(bind(Z, Target::L2)); p.bindings.push_back(bind(C, Target::R2));
    p.bindings.push_back(bind(I, Target::RightUp)); p.bindings.push_back(bind(K, Target::RightDown));
    p.bindings.push_back(bind(J, Target::RightLeft)); p.bindings.push_back(bind(L, Target::RightRight));
    p.bindings.push_back(bind(P, Target::Options)); p.bindings.push_back(bind(M, Target::Touchpad));
    p.bindings.push_back(bind(InputCode(F1 + 9), Target::PS));
    addProfileKeys(p);
    return p;
}
ProfileSet defaultProfiles() {
    ProfileSet s;
    s.profiles = {defaultOnFoot(), defaultCombat(), defaultDriving(), defaultMenus()};
    s.active = 0; s.mode = InputMode::Hybrid; s.priority = StickPriority::Larger;
    return s;
}

namespace {
// One table drives serialization, parsing and clamping for every numeric field.
struct FloatField { std::string_view name; float& (*get)(KbmProfile&); float lo, hi; };
#define VF(n, expr, lo, hi) FloatField{n, [](KbmProfile& p) -> float& { return p.expr; }, lo, hi}
const FloatField floatFields[] = {
    VF("mouse.dpi", mouse.dpi, 50, 64000), VF("mouse.sensitivity", mouse.sensitivity, 0.01f, 50),
    VF("mouse.sensitivityX", mouse.sensitivityX, 0.05f, 10), VF("mouse.sensitivityY", mouse.sensitivityY, 0.05f, 10),
    VF("mouse.adsMultiplier", mouse.adsMultiplier, 0.05f, 3), VF("mouse.precisionMultiplier", mouse.precisionMultiplier, 0.05f, 3),
    VF("mouse.antiDeadzone", mouse.antiDeadzone, 0, 0.6f), VF("mouse.gamma", mouse.curve.gamma, 0.2f, 5),
    VF("mouse.maxOutput", mouse.maxOutput, 0.3f, 1), VF("mouse.smoothingMs", mouse.smoothingMs, 0, 50),
    VF("mouse.acceleration", mouse.acceleration, 0, 4), VF("mouse.noiseFloor", mouse.noiseFloor, 0, 2000),
    VF("mouse.overflowCarryMs", mouse.overflowCarryMs, 0, 300), VF("mouse.minWindowMs", mouse.minWindowMs, 2, 20),
    VF("mouse.radialGain", mouse.radialGain, 0.1f, 10), VF("mouse.radialSelect", mouse.radialSelect, 0, 0.95f),
    VF("keys.riseMs", keys.riseMs, 0, 1000), VF("keys.fallMs", keys.fallMs, 0, 1000),
    VF("keys.triggerRiseMs", keys.triggerRiseMs, 0, 1000), VF("keys.wheelPulseMs", keys.wheelPulseMs, 16, 250),
};
#undef VF
template<class E> struct EnumName { E value; std::string_view name; };
constexpr EnumName<MouseTarget> mouseTargets[] = {{MouseTarget::RightStick, "right"}, {MouseTarget::LeftStick, "left"}, {MouseTarget::None, "none"}};
constexpr EnumName<CurveKind> curves[] = {{CurveKind::Linear, "linear"}, {CurveKind::Power, "power"}, {CurveKind::Table, "table"}};
constexpr EnumName<DeadzoneShape> shapes[] = {{DeadzoneShape::Radial, "radial"}, {DeadzoneShape::Axial, "axial"}};
constexpr EnumName<DiagonalMode> diagonals[] = {{DiagonalMode::Circular, "circular"}, {DiagonalMode::Square, "square"}};
constexpr EnumName<OppositeKeys> opposites[] = {{OppositeKeys::LastWins, "last"}, {OppositeKeys::Neutral, "neutral"}};
constexpr EnumName<InputMode> modes[] = {{InputMode::ControllerOnly, "controller"}, {InputMode::KeyboardMouse, "keyboardmouse"}, {InputMode::Hybrid, "hybrid"}};
constexpr EnumName<StickPriority> priorities[] = {{StickPriority::Larger, "larger"}, {StickPriority::Controller, "controller"}, {StickPriority::KeyboardMouse, "keyboardmouse"}};
template<class E, std::size_t N> std::string_view enumName(const EnumName<E> (&t)[N], E v) { for (auto& e : t) if (e.value == v) return e.name; return t[0].name; }
template<class E, std::size_t N> std::optional<E> enumParse(const EnumName<E> (&t)[N], std::string_view s) { for (auto& e : t) if (e.name == s) return e.value; return std::nullopt; }

std::string fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g", double(v));
    return buf;
}
std::optional<float> number(std::string_view s) {
    // from_chars for float is not universally available on older libc++; strtof on a copy.
    if (s.empty() || s.size() > 31) return std::nullopt;
    char buf[32]; std::copy(s.begin(), s.end(), buf); buf[s.size()] = 0;
    char* end = nullptr; const float v = std::strtof(buf, &end);
    if (end != buf + s.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
std::string codeText(InputCode c) {
    const auto n = codeName(c);
    if (!n.empty()) return std::string(n);
    char buf[16]; std::snprintf(buf, sizeof buf, "VK0x%02X", unsigned(c) & 0xFFu);
    return buf;
}
std::string bindingTextImpl(const Binding& b) {
    std::string out = b.modifier ? codeText(b.modifier) + "+" + codeText(b.key) : codeText(b.key);
    out += ">"; out += targetName(b.target);
    if (b.behavior != Behavior::Hold) { out += ":"; out += behaviorName(b.behavior); }
    if (b.value != 1.f) out += ":v=" + fmt(b.value);
    if (b.behavior == Behavior::Repeat) out += ":hz=" + fmt(b.repeatHz);
    if (b.radial) out += ":radial";
    return out;
}
std::optional<Binding> parseBinding(std::string_view s, std::string& why) {
    const auto arrow = s.find('>');
    if (arrow == std::string_view::npos) { why = "missing '>'"; return std::nullopt; }
    Binding b;
    const auto keys = trim(s.substr(0, arrow));
    const auto plus = keys.find('+');
    const auto key = parseCode(trim(plus == std::string_view::npos ? keys : keys.substr(plus + 1)));
    if (!key) { why = "unknown key"; return std::nullopt; }
    b.key = *key;
    if (plus != std::string_view::npos) {
        const auto mod = parseCode(trim(keys.substr(0, plus)));
        if (!mod) { why = "unknown modifier"; return std::nullopt; }
        b.modifier = *mod;
    }
    auto rest = s.substr(arrow + 1);
    bool first = true;
    while (true) {
        const auto colon = rest.find(':');
        const auto token = trim(rest.substr(0, colon));
        if (first) {
            const auto t = parseTarget(token);
            if (!t || *t == Target::None) { why = "unknown target"; return std::nullopt; }
            b.target = *t; first = false;
        } else if (auto beh = parseBehavior(token)) b.behavior = *beh;
        else if (token == "radial") b.radial = true;
        else if (token.starts_with("v=")) { auto v = number(token.substr(2)); if (!v) { why = "bad value"; return std::nullopt; } b.value = *v; }
        else if (token.starts_with("hz=")) { auto v = number(token.substr(3)); if (!v) { why = "bad repeat rate"; return std::nullopt; } b.repeatHz = *v; }
        else if (!token.empty()) { why = "unknown option"; return std::nullopt; }
        if (colon == std::string_view::npos) break;
        rest = rest.substr(colon + 1);
    }
    return b;
}
void writeProfile(std::string& out, const KbmProfile& src) {
    KbmProfile p = src; // field getters need a mutable reference
    out += "[profile]\n";
    out += "name=" + p.name + "\n";
    out += "mouse.target="; out += enumName(mouseTargets, p.mouseTarget); out += "\n";
    for (const auto& f : floatFields) { out += f.name; out += "=" + fmt(f.get(p)) + "\n"; }
    out += "mouse.invertY="; out += p.mouse.invertY ? "1\n" : "0\n";
    out += "mouse.curve="; out += enumName(curves, p.mouse.curve.kind); out += "\n";
    if (!p.mouse.curve.table.empty()) {
        out += "mouse.table=";
        for (std::size_t i = 0; i < p.mouse.curve.table.size(); ++i) { if (i) out += ","; out += fmt(p.mouse.curve.table[i].deflection) + "/" + fmt(p.mouse.curve.table[i].rate); }
        out += "\n";
    }
    out += "mouse.deadzoneShape="; out += enumName(shapes, p.mouse.deadzoneShape); out += "\n";
    out += "mouse.diagonal="; out += enumName(diagonals, p.mouse.diagonal); out += "\n";
    out += "keys.opposite="; out += enumName(opposites, p.keys.opposite); out += "\n";
    out += "keys.diagonal="; out += enumName(diagonals, p.keys.diagonal); out += "\n";
    out += "adsOnL2="; out += p.adsOnL2 ? "1\n" : "0\n";
    for (const auto& b : p.bindings) out += "bind=" + bindingTextImpl(b) + "\n";
}
bool applyProfileLine(KbmProfile& p, std::string_view k, std::string_view v, std::string& why) {
    if (k == "name") { p.name = std::string(v.substr(0, MaxNameLength)); return true; }
    if (k == "bind") { auto b = parseBinding(v, why); if (!b) return false; p.bindings.push_back(*b); return true; }
    for (const auto& f : floatFields) if (f.name == k) { auto n = number(v); if (!n) { why = "not a number"; return false; } f.get(p) = *n; return true; }
    const auto boolean = [&](bool& dst) { if (v == "1" || v == "true") dst = true; else if (v == "0" || v == "false") dst = false; else { why = "not 0/1"; return false; } return true; };
    if (k == "mouse.invertY") return boolean(p.mouse.invertY);
    if (k == "adsOnL2") return boolean(p.adsOnL2);
    const auto pick = [&](auto& table, auto& dst) { auto e = enumParse(table, v); if (!e) { why = "unknown value"; return false; } dst = *e; return true; };
    if (k == "mouse.target") return pick(mouseTargets, p.mouseTarget);
    if (k == "mouse.curve") return pick(curves, p.mouse.curve.kind);
    if (k == "mouse.deadzoneShape") return pick(shapes, p.mouse.deadzoneShape);
    if (k == "mouse.diagonal") return pick(diagonals, p.mouse.diagonal);
    if (k == "keys.opposite") return pick(opposites, p.keys.opposite);
    if (k == "keys.diagonal") return pick(diagonals, p.keys.diagonal);
    if (k == "mouse.table") {
        p.mouse.curve.table.clear();
        while (!v.empty()) {
            const auto comma = v.find(',');
            const auto item = trim(v.substr(0, comma));
            const auto slash = item.find('/');
            if (slash == std::string_view::npos) { why = "bad table point"; return false; }
            auto d = number(trim(item.substr(0, slash))), r = number(trim(item.substr(slash + 1)));
            if (!d || !r || p.mouse.curve.table.size() >= 32) { why = "bad table point"; return false; }
            p.mouse.curve.table.push_back({*d, *r});
            if (comma == std::string_view::npos) break;
            v = v.substr(comma + 1);
        }
        return true;
    }
    why = "unknown key";
    return false;
}
} // namespace

std::vector<std::string> sanitize(KbmProfile& p) {
    std::vector<std::string> notes;
    if (p.name.empty()) { p.name = "Profile"; notes.push_back("empty name replaced"); }
    if (p.name.size() > MaxNameLength) { p.name.resize(MaxNameLength); notes.push_back("name truncated"); }
    for (char& c : p.name) if (c == '\n' || c == '\r' || c == '[' || c == ']') c = ' ';
    for (const auto& f : floatFields) {
        float& v = f.get(p);
        const float fixed = std::isfinite(v) ? std::clamp(v, f.lo, f.hi) : f.lo;
        if (fixed != v) { notes.push_back(std::string(f.name) + " clamped"); v = fixed; }
    }
    if (p.mouse.curve.kind == CurveKind::Table && !p.mouse.curve.valid()) {
        p.mouse.curve.kind = CurveKind::Linear; p.mouse.curve.table.clear();
        notes.push_back("invalid calibration table discarded; linear curve used");
    }
    const auto before = p.bindings.size();
    std::erase_if(p.bindings, [](const Binding& b) {
        return !validCode(b.key) || (b.modifier && (!validCode(b.modifier) || b.modifier == b.key || isWheel(b.modifier))) ||
               b.target == Target::None || b.target >= Target::Count;
    });
    if (p.bindings.size() != before) notes.push_back("invalid bindings removed");
    if (p.bindings.size() > MaxBindings) { p.bindings.resize(MaxBindings); notes.push_back("binding list truncated"); }
    for (auto& b : p.bindings) {
        if (!std::isfinite(b.value)) b.value = 1;
        if (b.target == Target::SwitchProfile) b.value = std::clamp(std::round(b.value), 0.f, float(MaxProfiles - 1));
        else b.value = std::clamp(b.value, 0.05f, 1.f);
        if (!std::isfinite(b.repeatHz)) b.repeatHz = 10;
        b.repeatHz = std::clamp(b.repeatHz, 1.f, 30.f);
        if (isWheel(b.key) && b.behavior == Behavior::Toggle) b.behavior = Behavior::Hold; // a notch has no "held"
        if (b.radial && b.behavior != Behavior::Hold) b.behavior = Behavior::Hold;
    }
    return notes;
}

std::vector<std::string> sanitize(ProfileSet& s) {
    std::vector<std::string> notes;
    if (s.profiles.empty()) { s.profiles = defaultProfiles().profiles; notes.push_back("no profiles; defaults restored"); }
    if (s.profiles.size() > MaxProfiles) { s.profiles.resize(MaxProfiles); notes.push_back("profile list truncated"); }
    for (auto& p : s.profiles) for (auto& n : sanitize(p)) notes.push_back(p.name + ": " + n);
    if (s.active < 0 || s.active >= int(s.profiles.size())) { s.active = 0; notes.push_back("active profile reset"); }
    return notes;
}

std::vector<Conflict> findConflicts(const KbmProfile& p) {
    std::vector<Conflict> out;
    for (std::size_t i = 0; i < p.bindings.size(); ++i) {
        const auto& a = p.bindings[i];
        for (std::size_t j = i + 1; j < p.bindings.size(); ++j) {
            const auto& b = p.bindings[j];
            if (a.key == b.key && a.modifier == b.modifier)
                out.push_back({i, j, true, bindingLabel(a) + " / " + bindingLabel(b) + ": same key drives two actions"});
        }
        if (a.radial) {
            for (std::size_t j = 0; j < p.bindings.size(); ++j)
                if (j != i && p.bindings[j].radial && j > i) out.push_back({i, j, false, "two radial-menu keys share the mouse; only one menu should be open at a time"});
        }
        if (a.modifier) {
            for (std::size_t j = 0; j < p.bindings.size(); ++j)
                if (p.bindings[j].key == a.modifier && !p.bindings[j].modifier)
                    out.push_back({i, j, false, std::string(codeName(a.modifier)) + " is a modifier and also drives " + std::string(targetName(p.bindings[j].target)) + " by itself"});
        }
    }
    return out;
}

std::string serializeProfile(const KbmProfile& p) {
    std::string out = "# Veyra PS5 keyboard/mouse profile\nformat=1\n";
    writeProfile(out, p);
    return out;
}

std::string serialize(const ProfileSet& s) {
    std::string out = "# Veyra PS5 keyboard/mouse profiles. Edit in the app; unknown lines are ignored.\nformat=1\n";
    out += "mode="; out += enumName(modes, s.mode); out += "\n";
    out += "priority="; out += enumName(priorities, s.priority); out += "\n";
    out += "active=" + std::to_string(s.active) + "\n";
    for (const auto& p : s.profiles) writeProfile(out, p);
    return out;
}

ParseResult parse(std::string_view text) {
    ParseResult r;
    if (text.size() > 512 * 1024) { r.error = "file too large"; return r; }
    ProfileSet s; s.profiles.clear();
    bool sawFormat = false;
    int active = 0, lineNo = 0;
    KbmProfile* current = nullptr;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        const auto line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++lineNo;
        if (line.empty() || line.front() == '#') continue;
        if (line == "[profile]") {
            if (s.profiles.size() >= MaxProfiles) { r.warnings.push_back("too many profiles; extra ignored"); current = nullptr; continue; }
            KbmProfile p; p.bindings.clear();
            s.profiles.push_back(std::move(p)); current = &s.profiles.back();
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) { r.warnings.push_back("line " + std::to_string(lineNo) + ": ignored"); continue; }
        const auto k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (!current) {
            if (k == "format") { if (v != "1") { r.error = "unsupported format version"; return r; } sawFormat = true; }
            else if (k == "mode") { if (auto e = enumParse(modes, v)) s.mode = *e; else r.warnings.push_back("unknown mode"); }
            else if (k == "priority") { if (auto e = enumParse(priorities, v)) s.priority = *e; else r.warnings.push_back("unknown priority"); }
            else if (k == "active") { auto n = number(v); active = n ? int(*n) : 0; }
            else r.warnings.push_back("line " + std::to_string(lineNo) + ": unknown key");
            continue;
        }
        std::string why;
        if (!applyProfileLine(*current, k, v, why)) r.warnings.push_back("line " + std::to_string(lineNo) + ": " + why);
    }
    if (!sawFormat) { r.error = "not a Veyra keyboard/mouse profile file"; return r; }
    if (s.profiles.empty()) { r.error = "no profiles in file"; return r; }
    s.active = active;
    for (auto& n : sanitize(s)) r.warnings.push_back(n);
    r.set = std::move(s);
    return r;
}

std::optional<KbmProfile> parseProfile(std::string_view text, std::vector<std::string>* warnings) {
    auto r = parse(text);
    if (warnings) *warnings = r.warnings;
    if (!r.set) { if (warnings) warnings->push_back(r.error); return std::nullopt; }
    return r.set->profiles.front();
}

std::string bindingText(const Binding& b) { return bindingTextImpl(b); }
std::optional<Binding> parseBindingText(std::string_view s, std::string* why) {
    std::string w;
    auto b = parseBinding(trim(s), w);
    if (why) *why = w;
    return b;
}

std::vector<std::pair<std::string, std::string>> profileFields(const KbmProfile& profile) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string text;
    writeProfile(text, profile);
    std::string_view rest = text;
    while (!rest.empty()) {
        const auto nl = rest.find('\n');
        const auto line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const auto k = line.substr(0, eq);
        if (k == "name" || k == "bind") continue;
        out.emplace_back(std::string(k), std::string(line.substr(eq + 1)));
    }
    return out;
}

bool setProfileField(KbmProfile& p, std::string_view key, std::string_view value, std::string* why) {
    if (key == "name" || key == "bind") { if (why) *why = "not a field"; return false; }
    KbmProfile copy = p;
    std::string w;
    if (!applyProfileLine(copy, trim(key), trim(value), w)) { if (why) *why = w; return false; }
    (void)sanitize(copy);
    p = std::move(copy);
    return true;
}

std::string bindingLabel(const Binding& b) {
    std::string out = b.modifier ? codeText(b.modifier) + "+" + codeText(b.key) : codeText(b.key);
    out += " -> "; out += targetName(b.target);
    if (b.behavior != Behavior::Hold) { out += " ("; out += behaviorName(b.behavior); out += ")"; }
    if (b.radial) out += " (radial)";
    return out;
}

} // namespace veyra::remoteplay::kbm
