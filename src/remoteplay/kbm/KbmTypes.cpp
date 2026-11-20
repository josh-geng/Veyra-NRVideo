// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/kbm/KbmTypes.h"
#include <algorithm>
#include <array>
#include <cctype>

namespace veyra::remoteplay::kbm {
namespace {
struct Named { InputCode code; std::string_view name; };
constexpr Named fixedNames[] = {
    {code::Backspace, "Backspace"}, {code::Tab, "Tab"}, {code::Enter, "Enter"}, {code::Pause, "Pause"},
    {code::CapsLock, "CapsLock"}, {code::Escape, "Escape"}, {code::Space, "Space"},
    {code::PageUp, "PageUp"}, {code::PageDown, "PageDown"}, {code::End, "End"}, {code::Home, "Home"},
    {code::Left, "Left"}, {code::Up, "Up"}, {code::Right, "Right"}, {code::Down, "Down"},
    {code::Insert, "Insert"}, {code::Delete, "Delete"},
    {code::LShift, "LShift"}, {code::RShift, "RShift"}, {code::LCtrl, "LCtrl"}, {code::RCtrl, "RCtrl"},
    {code::LAlt, "LAlt"}, {code::RAlt, "RAlt"},
    {code::Semicolon, "Semicolon"}, {code::Equals, "Equals"}, {code::Comma, "Comma"}, {code::Minus, "Minus"},
    {code::Period, "Period"}, {code::Slash, "Slash"}, {code::Grave, "Grave"}, {code::LBracket, "LBracket"},
    {code::Backslash, "Backslash"}, {code::RBracket, "RBracket"}, {code::Quote, "Quote"},
    {code::MouseLeft, "Mouse1"}, {code::MouseRight, "Mouse2"}, {code::MouseMiddle, "Mouse3"},
    {code::MouseX1, "Mouse4"}, {code::MouseX2, "Mouse5"},
    {code::WheelUp, "WheelUp"}, {code::WheelDown, "WheelDown"}, {code::WheelLeft, "WheelLeft"}, {code::WheelRight, "WheelRight"},
};
// Generated names: digits, letters, F-keys, numpad.
struct Generated { std::array<std::array<char, 8>, 256> text{}; };
const Generated& generated() {
    static const Generated g = [] {
        Generated out;
        const auto put = [&](int c, std::string_view s) { std::copy(s.begin(), s.end(), out.text[c].begin()); };
        for (int c = '0'; c <= '9'; ++c) { char s[2] = {char(c), 0}; put(c, s); }
        for (int c = 'A'; c <= 'Z'; ++c) { char s[2] = {char(c), 0}; put(c, s); }
        for (int i = 0; i < 24; ++i) { char s[4] = {'F', char(i + 1 >= 10 ? '0' + (i + 1) / 10 : '0' + i + 1), char(i + 1 >= 10 ? '0' + (i + 1) % 10 : 0), 0}; put(0x70 + i, s); }
        for (int i = 0; i < 10; ++i) { char s[5] = {'N', 'u', 'm', char('0' + i), 0}; put(0x60 + i, s); }
        return out;
    }();
    return g;
}
struct TargetNamed { Target target; std::string_view name; };
constexpr TargetNamed targetNames[] = {
    {Target::None, "None"}, {Target::Cross, "Cross"}, {Target::Circle, "Circle"}, {Target::Square, "Square"},
    {Target::Triangle, "Triangle"}, {Target::L1, "L1"}, {Target::R1, "R1"}, {Target::L2, "L2"}, {Target::R2, "R2"},
    {Target::L3, "L3"}, {Target::R3, "R3"}, {Target::DpadUp, "DpadUp"}, {Target::DpadDown, "DpadDown"},
    {Target::DpadLeft, "DpadLeft"}, {Target::DpadRight, "DpadRight"}, {Target::Options, "Options"},
    {Target::Create, "Create"}, {Target::Touchpad, "Touchpad"}, {Target::PS, "PS"},
    {Target::LeftUp, "LeftUp"}, {Target::LeftDown, "LeftDown"}, {Target::LeftLeft, "LeftLeft"}, {Target::LeftRight, "LeftRight"},
    {Target::RightUp, "RightUp"}, {Target::RightDown, "RightDown"}, {Target::RightLeft, "RightLeft"}, {Target::RightRight, "RightRight"},
    {Target::WalkModifier, "WalkModifier"}, {Target::PrecisionModifier, "PrecisionModifier"}, {Target::SwitchProfile, "SwitchProfile"},
};
bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
}
} // namespace

std::string_view codeName(InputCode c) noexcept {
    for (const auto& n : fixedNames) if (n.code == c) return n.name;
    if (c < 256) { const auto& t = generated().text[c]; if (t[0]) return std::string_view(t.data()); }
    return {};
}

std::optional<InputCode> parseCode(std::string_view s) noexcept {
    if (s.empty() || s.size() > 16) return std::nullopt;
    for (const auto& n : fixedNames) if (iequal(n.name, s)) return n.code;
    for (int c = 1; c < 256; ++c) { const auto& t = generated().text[c]; if (t[0] && iequal(std::string_view(t.data()), s)) return InputCode(c); }
    // Raw virtual-key escape hatch for unnamed keys: "VK0x5B".
    if (s.size() == 6 && iequal(s.substr(0, 4), "VK0x")) {
        unsigned v = 0;
        for (char ch : s.substr(4)) { v <<= 4; if (ch >= '0' && ch <= '9') v |= unsigned(ch - '0'); else if (std::isxdigit((unsigned char)ch)) v |= unsigned(std::tolower((unsigned char)ch) - 'a' + 10); else return std::nullopt; }
        if (validCode(InputCode(v))) return InputCode(v);
    }
    return std::nullopt;
}

std::string_view targetName(Target t) noexcept {
    for (const auto& n : targetNames) if (n.target == t) return n.name;
    return "None";
}
std::optional<Target> parseTarget(std::string_view s) noexcept {
    for (const auto& n : targetNames) if (iequal(n.name, s)) return n.target;
    if (iequal(s, "Share")) return Target::Create;
    return std::nullopt;
}
std::string_view behaviorName(Behavior b) noexcept {
    switch (b) { case Behavior::Hold: return "hold"; case Behavior::Toggle: return "toggle"; case Behavior::Repeat: return "repeat"; }
    return "hold";
}
std::optional<Behavior> parseBehavior(std::string_view s) noexcept {
    if (iequal(s, "hold")) return Behavior::Hold;
    if (iequal(s, "toggle")) return Behavior::Toggle;
    if (iequal(s, "repeat")) return Behavior::Repeat;
    return std::nullopt;
}
bool isPadButton(Target t) noexcept {
    switch (t) {
    case Target::Cross: case Target::Circle: case Target::Square: case Target::Triangle: case Target::L1: case Target::R1:
    case Target::L3: case Target::R3: case Target::DpadUp: case Target::DpadDown: case Target::DpadLeft: case Target::DpadRight:
    case Target::Options: case Target::Create: case Target::Touchpad: case Target::PS: return true;
    default: return false;
    }
}
} // namespace veyra::remoteplay::kbm
