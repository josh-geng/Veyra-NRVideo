# PS5 keyboard & mouse for Veyra — engineering report

Date 2026-11-20 · branch `claude/fervent-lamport-xa6ms2` · base `9c0c535` (main, app version 2.1.9,
unchanged). **Status: implementation complete for review; NOT a release candidate.** The portable engine has
been built with MSVC and its tests pass on Windows; the Windows-specific capture, bridge and QML
code has not been compiled with MSVC, and nothing has been tested against a PS5, GTA VI or an RTX 5080. A release
candidate requires the Windows build and the manual checklist in [MEASUREMENT.md](MEASUREMENT.md).

Companion documents: [ARCHITECTURE](ARCHITECTURE.md) · [USER_GUIDE](USER_GUIDE.md) ·
[TROUBLESHOOTING](TROUBLESHOOTING.md) · [MEASUREMENT](MEASUREMENT.md).

## 1. Repository audit (facts established from the code)

* **Stack.** Windows x64, C++20, CMake/Ninja + MSVC, Qt 6.8.3 QML UI over a large `QmlPlayerBridge`,
  D3D12 renderer, FFmpeg (patched) decode with D3D12VA, SDL3 for physical controllers, chiaki-ng
  pinned at `0e16950…` with three Veyra patches. The top-level CMake builds only the Linux OBS filter
  on non-Windows hosts.
* **Remote Play lifecycle.** `QmlPlayerBridge::ps5Connect` → `EngineController::openRemotePlay` →
  `RemotePlaySessionSource` owner thread (connect, decode, 4 ms controller submission, recovery with
  bounded retries and RP_IN_USE back-off) → `RemotePlaySource` → `ChiakiBackend` (single owner thread
  contract). Controller-only mode ("PS5 手柄控制", capture-card picture) uses the same source with
  media dropped.
* **Controller path before this change.** SDL pad polled by a UI `QTimer` every 8 ms →
  `EngineController::remotePlayController` → bounded edge-preserving queue (16) with 100 ms staleness
  → owner `takeControllerLocked` every 4 ms → `chiaki_session_set_controller_state`. There was **no
  keyboard/mouse path to the PS5**; keyboard/mouse capture existed only for Moonlight (PC streaming).
* **Chiaki cadence.** Feedback-state packets go out on every state change (the 8 ms minimum is an
  unimplemented TODO upstream) plus a 200 ms keep-alive; button edges via the patched history channel.
* **Video / enhancement / FG (audited, unchanged).** Official NGX DLSS Super Resolution and DLSS
  Frame Generation (DLSSG, multi-frame count from the runtime's capability), RTX Video SDK 1.1 Video
  Super Resolution and TrueHDR, an experimental local-only DLSS-NR adapter (Feature 18, off unless
  `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR`), a third-party "ANR" DLL never registered with NGX core, XeSS /
  FSR 3.1 / FSR 4 ML / AMD AMF FRC / Veyra VFG frame-generation backends, duplicate-frame removal
  ("60→30", automatic), and separate telemetry for received, decoded, content, submission and
  presented FPS. **There is no "DLSS 5" anywhere in this code base**; nothing in this delivery claims
  one. The repository also contains architecture-spoof "MFG unlock" paths for Ampere/Ada; an RTX 5080
  (Blackwell) does not need them and this delivery does not touch them.
* **Persistence.** INI (`settings.ini`) for Remote Play options, DPAPI-encrypted pairing profiles in
  `%LOCALAPPDATA%\Veyra\remoteplay`, JSON preferences for the player.
* **Tests/CI.** Many standalone test executables registered with CTest on Windows, Qt Quick tests
  globbed from `tests/qml/quick`, acceptance scripts under `scripts/acceptance`. GitHub Actions has
  focused workflows only (`obs-filter-checks`, `vrr-profile-checks`, `star-history`); the full product
  is built locally on the maintainer's machine. This delivery adds `ps5-kbm-checks` (Linux
  ASan/UBSan/TSan and Windows MSVC + Qt 6.8.3, see §5).
* **Licences.** GPL-3.0-only project; chiaki-ng AGPL-3.0 with its OpenSSL exception; no new
  third-party dependency was added by this work.

## 2. Features implemented

| Area | Implemented | Verified here |
|---|---|---|
| Raw-input capture (mouse + keyboard), dedicated high-priority thread, per-report foreground check, device-removal release, Ctrl+Alt+Shift+Z, cursor hide/clip, legacy-message suppression | `Ps5KbmCapture` | type-checked (MinGW headers) |
| Mouse → stick: polling-rate-independent velocity, DPI normalization, X/Y, ADS, precision, invert, anti-dead zone radial/axial, linear/power/table curves, ceiling, smoothing (default 0), acceleration (default 0), bounded overflow carry (default 0), circular/square diagonals, immediate stop | `MouseStick` | unit tests |
| Keyboard: WASD stick with SOCD, walk modifier, slew, triggers with ramp, all DualSense buttons incl. Options/Create/Touchpad/PS, mouse buttons 1–5, wheel notches, hold/toggle/repeat, modifiers, conflicts | `KbmEngine`, `KbmProfile` | unit tests |
| Weapon wheel / radial menus: mouse positions the stick while held | `MouseStick` radial mode | unit tests |
| Profiles: On foot, Combat, Driving, Menus; F1–F4 switching with held-key suppression; new/duplicate/rename/reset/delete; import/export; versioned file; corrupted-file recovery | `KbmProfile`, bridge | unit tests (format), type-checked (bridge) |
| Hybrid / KBM-only / controller-only, stick priority, pad touch/motion kept, feedback unchanged | `mergeStates` | unit tests |
| Guided calibration: dead-zone sweep, turn-rate probe, inverse table, saturation and time-ramp detection, linearity check | `Calibration`, bridge, dialog | unit tests (logic) |
| Sampling at submission time inside the Remote Play owner loop (stream and controller-only) | `RemotePlaySessionSource` | type-checked |
| Streaming-mode idle wait on a high-resolution timer (fixes a possible 15.6 ms `sleep_for(1ms)`) | `RemotePlaySessionSource` | type-checked; effect unmeasured |
| Live diagnostics: event rates, report interval, processing µs, submit interval, saturation, stick/trigger meters | `KbmStats`, dialog | unit tests (stats), parse-checked (QML) |
| Stream presets Low latency / Balanced / Quality first / Custom over existing options | `Ps5SourceDialog.qml` | parse-checked |
| UI strings in zh, zh-TW, en, ja, ko | `i18n/catalog.json` | `extract.py --check`: 0 missing, 0 placeholder errors |

## 3. Intentionally unchanged

* Chiaki protocol, patches, pairing/authentication/credential storage, discovery, wake, recovery and
  back-off logic, queue limits and jitter buffering (no evidence available to retune them).
* Decoder selection and D3D12VA path, renderer, NR/SR/VSR/TrueHDR, all frame-generation backends,
  dedup, presentation/VRR defaults, virtual camera, recording. No measurement here justified a change.
* SDL pad handling, rumble, adaptive triggers, haptics, gyro calibration.
* The session owner's single-thread contract with `ChiakiBackend` (see §6 for the trade-off).
* Default behaviour: the feature is off by default; with it off the engine returns the pad state
  unchanged (unit-tested), so existing users see no difference.
* App version (2.1.9), release assets, GitHub releases.

## 4. Files

New: `.github/workflows/ps5-kbm-checks.yml`; `cmake/VeyraKbm.cmake`; `include/veyra/remoteplay/kbm/{KbmTypes,MouseStick,KbmProfile,KbmEngine,Calibration}.h`;
`src/remoteplay/kbm/*.cpp`; `include/veyra/ui/Ps5KbmCapture.h`; `src/ui/Ps5KbmCapture.cpp`;
`src/ui/bridge/Ps5Kbm.cpp`; `qml/Veyra/Ps5KbmDialog.qml`; `tests/kbm/{CMakeLists.txt,KbmTests.cpp}`;
`tests/qml/quick/tst_ps5_kbm.qml`; `docs/ps5-kbm/*`.

Modified: `CMakeLists.txt` (2 source lines), `cmake/VeyraRemotePlay.cmake` (link the engine),
`include/veyra/source/RemotePlaySource.h` (descriptor field), `src/source/RemotePlaySessionSource.cpp`
(sample before submit ×2, high-resolution idle wait), `include/veyra/ui/QmlPlayerBridge.h`
(properties/invokables/signals), `src/ui/QmlPlayerBridge_p.h` (state), `src/ui/QmlPlayerBridge.cpp`
(tick), `src/ui/bridge/RemotePlay.cpp` (attach engine to both session types),
`qml/Veyra/{DialogHost,Main,Ps5SourceDialog}.qml`, `qml/Veyra/qmldir`, `i18n/catalog.json`
(additions only), `docs/{CURRENT_STATUS,DEVELOPMENT,WORKLOG}.md`.

## 5. Build and test status (exact)

| Step | Command / environment | Result |
|---|---|---|
| Engine + tests, GCC 13, Debug, ASan+UBSan | `cmake -S tests/kbm -B … -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined` | 47/47 pass, no sanitizer reports |
| Engine + tests, GCC 13 Release, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` | standalone project, `ctest` | 47/47 pass, 0 warnings |
| Engine + tests, Clang 18 Release | same | 47/47 pass, 0 warnings |
| Concurrency test, GCC TSan | `veyra_kbm_tests concurrent_producer_consumer` | pass, no races reported |
| **Engine + tests, MSVC on the user's Windows 11 PC** (2026-11-21) | `cl /nologo /std:c++20 /EHsc /O2 /W4 /permissive- /utf-8` with VS 2026 Build Tools 18.12 Insiders, MSVC 14.52.37025 | **0 warnings, 47/47 pass**, compose 0.47 µs avg / 4.40 µs worst |
| Engine + tests cross-compiled for Windows | MinGW-w64 GCC 13 (posix), `-static` | builds, 0 warnings; **not executed** (Wine in the container cannot run even a hello-world) |
| `Ps5KbmCapture.cpp` | MinGW-w64 `-fsyntax-only` with Qt 6.4 headers | 0 errors |
| `Ps5Kbm.cpp`, `RemotePlay.cpp`, `RemotePlaySessionSource.cpp` | MinGW-w64 `-fsyntax-only`, real include chain, FFmpeg headers, stubs for proprietary SDK headers only | 0 errors in changed code (verified the compiler reaches the code with an injected error) |
| QML | `qmlformat` (Qt 6.4) parse of every changed/new QML file | parse OK; **not executed** (needs Qt 6.8 + Veyra modules) |
| i18n | `python -B scripts/i18n/extract.py --check` | 3149 entries, 0 missing, 0 placeholder problems |
| GitHub Actions `ps5-kbm-checks`: engine + tests with MSVC `/W4`, Qt 6.8.3 compile of `Ps5KbmCapture.cpp` (with moc), Linux sanitizers | `.github/workflows/ps5-kbm-checks.yml` | **not run**: pushed 2026-11-20, but GitHub did not start either job ("account is locked due to a billing issue", run 44689673523). Re-run once Actions billing is resolved |
| Full product build (MSVC, Qt 6.8.3, Chiaki stage, NGX, FFmpeg) | — | **not run**: no Windows toolchain or proprietary SDKs in this environment |
| Existing Windows test suites (`veyra_remoteplay_*`, Qt Quick tests) | — | **not run** |
| PS5 / GTA VI / RTX 5080 hardware validation | — | **not run** |

## 6. Performance findings

* KBM processing: 0.75–0.82 µs average, 16–41 µs worst per 4 ms submission at 8 kHz mouse input on
  the CI host (Xeon 2.1 GHz VM). Engineering target (< 1 ms) met by a wide margin there.
* Latency structure improved by construction: KBM state is sampled in the owner loop at submission
  time, removing the 8 ms UI timer and UI-thread scheduling from the KBM path. Expected staleness
  ≤ 4 ms (owner) + the velocity window (≈ 2 ms average for a 1000 Hz mouse). This is reasoning, not a
  measured end-to-end figure.
* Risk found and fixed: streaming-mode idle `sleep_for(1ms)` could last 15.6 ms on Windows without a
  raised timer resolution (the same file documents this for the controller-only path), which would have
  stretched the 4 ms cadence. Now a high-resolution waitable timer, ≤ 1 ms, never past the next
  controller deadline. Benefit on the target PC is unmeasured; the live "submit interval" readout
  shows it directly.
* Remaining structural latency: in streaming mode the owner thread also decodes; a long decode delays
  the next submission. Not changed (backend single-owner contract); measure the submit-interval max
  first. If it regularly exceeds ~8 ms, the next step is a dedicated submission thread with the
  backend's owner check relaxed for `submitController` only, guarded by a mutex over the session
  pointer lifetime.

## 7. Unverified assumptions

1. GTA VI's controller layout matches the GTA V PlayStation layout used for defaults.
2. GTA VI's camera responds to the right stick through a dead zone plus a monotonic curve that the
   calibration can invert; any time-based acceleration is reported but cannot be removed.
3. The PS5 accepts and applies stick updates at Veyra's 4 ms cadence (DualSense USB rate).
4. `RIDEV_INPUTSINK` raw input on a message-only window in a second thread behaves as documented
   alongside Qt, SDL3 (HID game controllers) and the existing Moonlight capture (which registers
   raw mouse input only while a PC stream is captured — they are not captured at the same time).
5. The Qt 6.8 behaviour of the native event filter for posted keyboard/mouse messages matches Qt 6.4
   headers used for type-checking.
6. Stream presets (H.264 for lowest latency, 20 Mbps to save Wi-Fi airtime) are reasonable; not
   measured.

## 8. Risks

* **MSVC-only issues** in the Windows-specific files (capture, bridge, QML glue): the portable engine is
  warning-clean under MSVC `/W4` (2026-11-21), but those files were only type-checked with MinGW headers.
* **Raw-input registration conflicts** with other in-process users of mouse/keyboard raw input.
* **Focus edge cases** (overlays, Game Bar, multi-monitor fullscreen) — several independent release
  paths exist; real-world testing needed.
* **Anti-cheat / platform policy**: none apply to Remote Play input, which is a supported controller
  protocol; Veyra sends ordinary controller states and does not modify the console or the game.

## 9. Outstanding issues

* MSVC product build, existing Windows test suites and the new Qt Quick test must be run.
* Full manual checklist ([MEASUREMENT.md](MEASUREMENT.md)) on the target hardware.
* Per-device key tracking is not implemented: removing any keyboard/mouse releases everything.
* No in-game overlay/HUD for KBM state outside the dialog (the dialog's live section and logs only).
* Network RTT/jitter are not exposed by the pinned Chiaki API and are therefore not displayed.
* Automatic context detection (on foot vs vehicle) was deliberately not implemented: it would need
  fragile image recognition. Manual F1–F4 switching only.

## 10. Recommended next steps

1. Windows build: `scripts/acceptance/build-recording-handoff.py … veyra_qml_ui` plus
   `ctest -R remoteplay-kbm`; fix any MSVC warnings, then enable `/WX` on `veyra_remoteplay_kbm`.
2. Run `tst_ps5_kbm.qml` and the existing Qt Quick suite.
3. Hardware session following the checklist; record submit-interval statistics with CPU vs D3D12VA
   decode; only then decide on a dedicated submission thread.
4. Calibrate GTA VI and ship a measured curve table as an optional "GTA VI (calibrated)" profile.
5. Optional: per-device key state from `RAWINPUTHEADER.hDevice` so unplugging a second keyboard does
   not release keys held on the first.
