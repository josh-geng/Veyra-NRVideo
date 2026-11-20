// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Keyboard-and-mouse profiles: bindings, mouse tuning and keyboard-stick behaviour.
//
// The shipped "GTA-style" defaults pair common PC keys with the controller buttons of
// the GTA V PlayStation layout. They are an ASSUMPTION, not a verified GTA VI layout:
// check the game's own controller settings and edit the bindings to match.
#include "KbmTypes.h"
#include "MouseStick.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::remoteplay::kbm {

struct Binding {
    InputCode key = code::None;
    InputCode modifier = code::None; // must also be held; a modified binding shadows the plain one
    Target target = Target::None;
    Behavior behavior = Behavior::Hold;
    float value = 1.0f;   // trigger amount / stick magnitude / walk-precision scale / profile index
    float repeatHz = 10;  // Behavior::Repeat only (clamped so every phase spans ≥ 2 submissions)
    bool radial = false;  // while held, the mouse POSITIONS the right stick (weapon wheel)
    bool operator==(const Binding&) const = default;
};

struct KeyStickConfig {
    float riseMs = 0, fallMs = 0;  // keyboard left-stick slew (0 = instant). Driving: gradual steering.
    float triggerRiseMs = 0;       // keyboard L2/R2 ramp-up (release is always instant)
    OppositeKeys opposite = OppositeKeys::LastWins;
    DiagonalMode diagonal = DiagonalMode::Circular;
    float wheelPulseMs = 48;       // a wheel notch holds its target this long (≥ 2 submissions)
    bool operator==(const KeyStickConfig&) const = default;
};

struct KbmProfile {
    std::string name;
    MouseTarget mouseTarget = MouseTarget::RightStick;
    MouseConfig mouse;
    KeyStickConfig keys;
    bool adsOnL2 = true; // ADS multiplier applies while L2 is held (key, mouse button or pad)
    std::vector<Binding> bindings;
};

struct ProfileSet {
    InputMode mode = InputMode::Hybrid;
    StickPriority priority = StickPriority::Larger;
    std::vector<KbmProfile> profiles;
    int active = 0;
};

inline constexpr std::size_t MaxProfiles = 16, MaxBindings = 160, MaxNameLength = 48;

// Default set: On foot, Combat, Driving, Menus (GTA-style assumption, see above).
ProfileSet defaultProfiles();
KbmProfile defaultOnFoot();
KbmProfile defaultCombat();
KbmProfile defaultDriving();
KbmProfile defaultMenus();

// Clamp every numeric field into its documented range, drop invalid bindings,
// truncate names. Returns human-readable notes about anything that was changed.
std::vector<std::string> sanitize(KbmProfile&);
std::vector<std::string> sanitize(ProfileSet&);

struct Conflict {
    std::size_t first = 0, second = 0; // binding indices
    bool error = true;                 // false = warning (works, but probably unintended)
    std::string message;
};
std::vector<Conflict> findConflicts(const KbmProfile&);

// Text format (UTF-8, line based, versioned). Never contains credentials.
std::string serialize(const ProfileSet&);
std::string serializeProfile(const KbmProfile&); // single profile for import/export
struct ParseResult {
    std::optional<ProfileSet> set;   // empty on fatal error
    std::vector<std::string> warnings;
    std::string error;
};
ParseResult parse(std::string_view text);
// Accepts a single exported profile or a full set (first profile is used).
std::optional<KbmProfile> parseProfile(std::string_view text, std::vector<std::string>* warnings = nullptr);

std::string bindingLabel(const Binding&); // "LShift+F → Triangle (toggle)"
std::string bindingText(const Binding&);  // serialized form: "LShift+F>Triangle:toggle"
std::optional<Binding> parseBindingText(std::string_view, std::string* why = nullptr);

// Generic field access for settings UIs, using the same names as the file format
// ("mouse.sensitivity", "keys.riseMs", "mouse.curve", ...). Bindings and the name are
// not fields. setProfileField validates, clamps and returns false on a bad value.
std::vector<std::pair<std::string, std::string>> profileFields(const KbmProfile&);
bool setProfileField(KbmProfile&, std::string_view key, std::string_view value, std::string* why = nullptr);

} // namespace veyra::remoteplay::kbm
