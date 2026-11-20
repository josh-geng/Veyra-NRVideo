# PS5 keyboard & mouse control — architecture

Status: implemented on branch `claude/fervent-lamport-xa6ms2`, 2026-11-20. Unit-tested on Linux
(GCC/Clang, ASan/UBSan/TSan); Windows integration type-checked against MinGW-w64 headers only.
**Not yet built with MSVC, not run against a PS5.** See [ENGINEERING_REPORT.md](ENGINEERING_REPORT.md).

## What this is and what it is not

PS5 games receive a DualSense state: two analog sticks, two analog triggers, buttons, touchpad and
motion. A PS5 game that turns its camera from the right stick turns at a **rate** chosen by stick
deflection, capped at the game's maximum turn speed. A PC game turns by an **angle** proportional to
mouse counts, uncapped. No mapper can turn a rate-limited controller interface into true 1:1 mouse
aim. This subsystem makes stick rate track mouse speed as linearly and as quickly as the game's own
stick processing allows, and is explicit where it cannot (turn-rate ceiling, game-side acceleration
ramps, aim assist and smoothing inside the game).

## Data flow

```
Windows raw input (WM_INPUT, hardware counts, no pointer acceleration)
  │  dedicated raw-input thread, THREAD_PRIORITY_HIGHEST, message-only window
  │  RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, timestamp = steady clock (100 ns) on receipt
  ▼
kbm::KbmEngine::key / mouseMove / wheel        (O(1), allocation-free, one mutex)
  │    mouse reports → MouseStick ring buffer (512 entries, ≥ 64 ms at 8 kHz)
  │    key state → bitsets (down / latched / suppressed), toggles, wheel pulses
  ▼
RemotePlaySessionSource owner loop, every 4 ms
  │  pad  = takeControllerLocked(now)                 (existing SDL path, unchanged)
  │  state = kbm->compose(pad, now)                   ← sampled at submission time
  ▼
RemotePlaySource::submitController → ChiakiBackend → chiaki_session_set_controller_state
  ▼
Chiaki feedback sender → PS5
```

The Qt UI thread is **not** on this path. It only edits profiles, toggles capture, swallows legacy
window messages while captured, and reads statistics every ~250 ms.

## Verified submission cadence

Traced in this repository and in chiaki-ng at the pinned commit
`0e16950165f06e5c3291537c2eeba6e852be7120` (`lib/src/feedbacksender.c`):

| Stage | Cadence | Source |
|---|---|---|
| Veyra session owner → `chiaki_session_set_controller_state` | every 4 ms target. Controller-only mode already waited on a high-resolution waitable timer; streaming mode used `sleep_for(1 ms)` between frames, which the code itself documents can last 15.6 ms without raised timer resolution (Veyra never calls `timeBeginPeriod`). **Changed:** streaming mode now uses the same high-resolution timer, ≤ 1 ms and never past the next controller deadline | `src/source/RemotePlaySessionSource.cpp` |
| Chiaki feedback **state** packet (sticks, motion) | sent on **every change** — `FEEDBACK_STATE_TIMEOUT_MIN_MS` (8 ms) is defined but marked `TODO` and not enforced — plus a 200 ms keep-alive | upstream `feedbacksender.c` |
| Chiaki feedback **history** (button edges) | one event per packet, ordered releases → touches → presses, tail re-sends at 20/50/100/200 ms | Veyra patch `scripts/remoteplay/patches/0003-chiaki-feedback-history.patch` |
| Existing pad poll (SDL) | UI `QTimer` 8 ms | `src/ui/bridge/RemotePlay.cpp` |

Consequences:

* Before this change, any controller update could wait up to 8 ms (UI timer, coarse on Windows) +
  4 ms (owner). Keyboard/mouse is now sampled **in the owner loop**, so its staleness is bounded by
  the 4 ms owner cadence plus the velocity window (below), independent of UI load.
* 4 ms ≈ the 250 Hz DualSense USB report rate. Submitting faster was **not** done: whether the PS5
  or a given game consumes more than 250 states/s is unverified, and a higher packet rate costs Wi-Fi
  airtime.
* In streaming mode the owner thread also decodes video (`RemotePlaySource::read` →
  `avcodec_send_packet/receive_frame`). A slow decode delays the next controller submission. This was
  **not** restructured (the backend has a documented single-owner-thread contract protecting Chiaki
  state and motion integration). Instead `compose()` records the real interval between submissions
  (`submitIntervalAverageMs/MaxMs`), shown live in the dialog, so the effect can be measured on the
  target PC before deciding whether a dedicated submission thread is justified.

## Threads and ownership

| Thread | Owns | Touches engine via |
|---|---|---|
| Raw-input thread (`Ps5KbmCapture`) | message-only HWND, raw-input registration, wheel accumulators, hotkey modifier state | `key`, `mouseMove`, `wheel`, `setEnabled(false)` on focus/device loss |
| Remote Play session owner | Chiaki backend, decoder | `compose` |
| Qt UI thread | profiles, persistence, capture on/off, legacy-message filter, calibration | `setProfiles`, `selectProfile`, `setMode`, `setEnabled`, `setRightStickOverride`, `stats` |

`KbmEngine` holds one `std::mutex`; every call is short and never performs I/O. TSan run of the
producer/consumer/UI stress test is clean. The engine is shared (`std::shared_ptr`) between the bridge
and each session descriptor, so a session can outlive the UI object safely. `Ps5KbmCapture` joins its
thread in its destructor; the bridge declares it after the engine so it is destroyed first.

## Mouse → stick translation (`MouseStick`)

1. **Velocity estimator.** Each raw report's displacement is spread uniformly over the interval it
   covers (gap since the previous report, capped at 2× the estimated report interval; batched
   deliveries < 30 µs apart use the estimate). Speed = displacement overlapping
   `[tLast − W, tLast]` ÷ `W`, with `W = max(4 ms, 1.5 × report interval, time since last submit ≤ 25 ms)`.
   This gives the same output for 125 Hz and 8 kHz mice at the same physical speed (tested at
   125/500/1000/2000/4000/8000 Hz) and does not alternate 0/2× when a 125 Hz mouse is sampled at
   250 Hz.
2. **Stop.** If no report arrived for `1.5 × report interval + 0.5 ms`, speed is exactly 0 (≈ 2 ms for a
   1000 Hz mouse, ≈ 12.5 ms for 125 Hz). No decay tail unless the user enables smoothing.
3. **Normalization.** `rate = speed ÷ (DPI × 5 in/s ÷ sensitivity)` per axis, × axis scale, × ADS /
   precision multipliers. DPI is the user's reported mouse DPI so sensitivity is hardware-independent.
4. **Optional** acceleration `r·(1 + a·r)` (default off) and bounded overflow carry (default off):
   motion beyond the ceiling is held in a reservoir drained at the ceiling for ≤ N ms, dropped on
   direction reversal.
5. **Optional** EMA smoothing, default **0** (true zero smoothing — the code path is skipped).
6. **Shape.** Inverse of the game's curve: linear, power (`s = r^(1/γ)`), or a calibrated table
   (piecewise-linear inverse of measured deflection→rate). Anti-dead-zone added radially (default) or
   per axis. Magnitude clamped to `maxOutput`; direction preserved (circular) or mapped to the square.
7. `int16` with symmetric rounding; NaN/Inf can never reach the console.

Radial-menu mode (weapon wheel): while a `radial` binding is held, mouse counts **position** the right
stick (clamped to the unit circle, snapped to full deflection beyond `radialSelect`); motion during the
menu is never replayed into the camera afterwards; release returns the stick to centre.

## Keyboard mapping (`KbmEngine::compose`)

* Bindings map an input code (Windows VK or mouse button / wheel notch) plus optional modifier to a
  target: any DualSense button, L2/R2 (with amount), keyboard left/right-stick directions, walk
  modifier, precision modifier, profile switch.
* A modified binding shadows the plain binding of the same key while the modifier is held.
* Behaviours: hold, toggle, repeat (1–30 Hz, every phase ≥ 2 submissions).
* Opposite directions: last-pressed wins (default) or neutral. Circular diagonals (default) give the
  same speed in every direction.
* Slew (rise/fall ms) is time-based, so it is identical at 2, 4 or 8 ms submission intervals;
  reversing direction passes through centre at the fall rate (counter-steer).
* Taps shorter than one submission are latched: pressed for exactly one submission, then released.
* Wheel notches become discrete presses (default 48 ms) separated by gaps; at most 8 queued.

## Merge with the physical controller

`InputMode`: Controller only / Keyboard-mouse only / Hybrid. Hybrid produces **one** state per
submission: buttons OR-ed, triggers max, and **each stick from one source** (default: the larger
deflection, with a 0.15 threshold above typical DualSense resting drift; or prefer pad; or prefer
KBM). Touch, gyro and accelerometer always come from the pad. Rumble, adaptive triggers and haptics
are unchanged (they go to the physical DualSense if one is connected).

## Safety contract (stuck-input prevention)

Every one of these neutralizes all simulated inputs, toggles, wheel pulses, slews and mouse history
at the next submission: Ctrl+Alt+Shift+Z; foreground change (checked on the raw thread at least every
2 ms of input, independent of the UI thread); `WM_ACTIVATE`/`WM_KILLFOCUS` to a foreign window;
Qt application deactivation; raw-input device removal; session end; mode change; profile edit;
destructor. Keys still physically held across a profile switch are suppressed until released.
A 20 000-step randomized fuzz test (keys, mouse, wheel, enable/disable, profile switches) must end
neutral once all physical inputs are released.

## Persistence

`%LOCALAPPDATA%\Veyra\remoteplay\keyboard-mouse.txt` (same directory as the encrypted pairings;
the profile file itself contains no credentials). Versioned line format (`format=1`), written
atomically with `QSaveFile`, debounced 400 ms. Every value is range-clamped on load; invalid
bindings are dropped with warnings; an unreadable file is renamed to
`keyboard-mouse.corrupt-<timestamp>.txt` and defaults are used, with a user-visible message. The
feature switch is `[KeyboardMouse] Enabled` in `settings.ini` and defaults to **off**: with it off,
the engine passes the pad state through unchanged (tested).

## Files

| Path | Role |
|---|---|
| `include/veyra/remoteplay/kbm/*.h`, `src/remoteplay/kbm/*.cpp` | Portable engine (types, MouseStick, profiles, engine, calibration) |
| `cmake/VeyraKbm.cmake`, `tests/kbm/` | Library target and standalone test project |
| `include/veyra/ui/Ps5KbmCapture.h`, `src/ui/Ps5KbmCapture.cpp` | Win32 raw-input shim |
| `src/ui/bridge/Ps5Kbm.cpp` | QML bridge: settings, persistence, rebinding, calibration, stats |
| `qml/Veyra/Ps5KbmDialog.qml` | User interface |
| `src/source/RemotePlaySessionSource.cpp`, `include/veyra/source/RemotePlaySource.h` | Owner-loop hook (2 lines + descriptor field) |
