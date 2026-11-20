// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Keyboard-and-mouse → virtual DualSense: shared vocabulary.
//
// Portable by design: no <windows.h>, no Qt. The Win32 raw-input shim feeds Windows
// virtual-key codes as plain numbers and the whole translation is unit-tested on any
// host. Nothing here talks to the console; the output is a remoteplay::ControllerState
// that goes through the existing Chiaki submission path unchanged.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace veyra::remoteplay::kbm {

// 0x01..0xFE: Windows virtual-key codes (left/right modifiers are distinct: VK_LSHIFT etc.).
// 0x100+: mouse buttons and wheel notches. 0 = none.
using InputCode = std::uint16_t;
namespace code {
inline constexpr InputCode None = 0;
inline constexpr InputCode Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Pause = 0x13, CapsLock = 0x14, Escape = 0x1B, Space = 0x20;
inline constexpr InputCode PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24;
inline constexpr InputCode Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Insert = 0x2D, Delete = 0x2E;
inline constexpr InputCode Digit0 = 0x30; // '0'..'9' = 0x30..0x39, 'A'..'Z' = 0x41..0x5A
inline constexpr InputCode A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G', H = 'H', I = 'I', J = 'J', K = 'K', L = 'L', M = 'M';
inline constexpr InputCode N = 'N', O = 'O', P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U', V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z';
inline constexpr InputCode Numpad0 = 0x60, F1 = 0x70, F12 = 0x7B;
inline constexpr InputCode LShift = 0xA0, RShift = 0xA1, LCtrl = 0xA2, RCtrl = 0xA3, LAlt = 0xA4, RAlt = 0xA5;
inline constexpr InputCode Semicolon = 0xBA, Equals = 0xBB, Comma = 0xBC, Minus = 0xBD, Period = 0xBE, Slash = 0xBF, Grave = 0xC0;
inline constexpr InputCode LBracket = 0xDB, Backslash = 0xDC, RBracket = 0xDD, Quote = 0xDE;
inline constexpr InputCode MouseLeft = 0x101, MouseRight = 0x102, MouseMiddle = 0x103, MouseX1 = 0x104, MouseX2 = 0x105;
inline constexpr InputCode WheelUp = 0x110, WheelDown = 0x111, WheelLeft = 0x112, WheelRight = 0x113;
inline constexpr InputCode Max = 0x114; // exclusive bound for state tables
} // namespace code
inline constexpr bool isWheel(InputCode c) noexcept { return c >= code::WheelUp && c <= code::WheelRight; }
inline constexpr bool validCode(InputCode c) noexcept { return c > 0 && c < code::Max && (c < 0xFF || c > 0x100); }

// What a binding drives on the virtual controller.
enum class Target : std::uint8_t {
    None,
    Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3,
    DpadUp, DpadDown, DpadLeft, DpadRight, Options, Create, Touchpad, PS,
    LeftUp, LeftDown, LeftLeft, LeftRight,      // keyboard → left stick
    RightUp, RightDown, RightLeft, RightRight,  // keyboard → right stick (menus, fixed camera nudges)
    WalkModifier,       // scales the keyboard left stick (walk / gentle steering)
    PrecisionModifier,  // scales mouse sensitivity while held (sniper / fine aim)
    SwitchProfile,      // value = profile index
    Count
};
enum class Behavior : std::uint8_t { Hold, Toggle, Repeat };
enum class MouseTarget : std::uint8_t { RightStick, LeftStick, None };
enum class CurveKind : std::uint8_t { Linear, Power, Table };
enum class DeadzoneShape : std::uint8_t { Radial, Axial };
enum class DiagonalMode : std::uint8_t { Circular, Square };
enum class OppositeKeys : std::uint8_t { LastWins, Neutral };
// Which device drives the console. Hybrid merges both into ONE state per submission.
enum class InputMode : std::uint8_t { ControllerOnly, KeyboardMouse, Hybrid };
// In Hybrid, which stick source wins when both are deflected.
enum class StickPriority : std::uint8_t { Larger, Controller, KeyboardMouse };

std::string_view codeName(InputCode) noexcept;               // "W", "LShift", "Mouse1", "WheelUp"; "" if unnamed
std::optional<InputCode> parseCode(std::string_view) noexcept;
std::string_view targetName(Target) noexcept;                // stable serialization id
std::optional<Target> parseTarget(std::string_view) noexcept;
std::string_view behaviorName(Behavior) noexcept;
std::optional<Behavior> parseBehavior(std::string_view) noexcept;
// True for targets that are a DualSense button bit (not a stick, trigger or modifier).
bool isPadButton(Target) noexcept;

} // namespace veyra::remoteplay::kbm
