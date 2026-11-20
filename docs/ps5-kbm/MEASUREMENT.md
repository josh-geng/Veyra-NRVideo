# PS5 keyboard & mouse — performance measurement methodology and manual test checklist

## What Veyra measures, and what it does not

| Quantity | Where | Kind | Meaning / caveat |
|---|---|---|---|
| Mouse events/s, key events/s | KBM live panel | measured | Raw-input reports accepted while captured, over the last full second |
| Mouse report interval | KBM live panel | estimated | EMA of report spacing while moving. Mice only report when they move, so slow movement shows longer intervals than the polling rate |
| Processing time per update | KBM live panel | measured | Wall time of `KbmEngine::compose()` (average/max per second) — keyboard/mouse translation only |
| Submit interval | KBM live panel | measured | Spacing of `compose()` calls = controller submissions to Chiaki. Target 4 ms. Max reveals decode stalls or timer coarseness |
| Saturation % | KBM live panel | measured | Share of submissions where the mouse requested more than the stick ceiling |
| Stream received / decoded FPS, queue, IDR requests | `remoteplay-progress` log | measured | Complete access units from Chiaki, not network RTT |
| Packet window received/lost | `remoteplay-transport` log | measured upstream | Chiaki's resetting packet window, not cumulative UDP loss |
| Content / submission / presented FPS, 1 % low | existing performance panels | measured | Generated frames are labelled as such and never counted as source FPS |
| Network RTT / jitter | — | **not available** | The pinned Chiaki API does not expose RTT; Veyra does not invent it |
| Input-to-photon latency | — | **not measurable in software** | Requires external equipment (below) |

None of the in-app numbers is end-to-end latency. Processing time in particular is microseconds and
says nothing about Wi-Fi, PS5 encode, the game's input handling or display.

## Automated measurements in this delivery

`tests/kbm` (`processing_time_per_submission`): 2 500 submissions at 4 ms, 32 mouse reports per
submission (8 kHz), three keys held, Release build.

| Environment | compose() average | worst observed |
|---|---|---|
| CI container, Intel Xeon @ 2.10 GHz, 4 vCPU, Linux, GCC 13 Release | 0.75–0.82 µs | 16–41 µs (VM scheduling) |
| Same, ASan + UBSan Debug | 8–10 µs | 45–707 µs |
| **Target PC (Ryzen 7 9800X3D)** | **not measured** | **not measured** |

Engineering target "< 1 ms processing per event" is met by three orders of magnitude on the CI host;
it is not a claim about the target PC until measured there.

## How to measure on the target PC

1. **Submission cadence (no tools).** Stream GTA (or any game) with the KBM panel open on the live
   section, play for 5 minutes with frame generation off, then on. Record submit interval average/max.
   Expected: average ≈ 4.0 ms. A max > 10 ms means the session owner is being delayed; note decode
   mode (CPU vs D3D12VA) and repeat with the other.
2. **Polling-rate independence.** Set the mouse to 125, 500, 1000, 2000 Hz in its software. For each,
   run calibration step 3 (linearity) and note the slow/fast count ratio; also move at a steady speed
   and compare the right-stick meter. They should agree within a few percent.
3. **Turn-rate curve.** Calibration step 2 at 0.3 … 1.0. Save the generated report (screenshot).
4. **Input-to-photon (external camera).** 240 fps or faster camera (most phones' slow-motion mode is
   240 fps = 4.2 ms resolution). Film the mouse and the monitor in one frame. Flick the mouse from rest
   20 times; count frames from the first visible mouse motion to the first visible camera motion on
   screen. Report median and 90th percentile. Repeat: (a) KBM, frame generation off; (b) KBM, 2×
   frame generation; (c) DualSense in the same scene for a baseline. Without such equipment, no
   end-to-end latency number should be published.
5. **CPU/GPU.** Windows Task Manager or PresentMon: idle Veyra, Veyra streaming without KBM, with KBM
   captured and the mouse moving at 1000 Hz. The raw-input thread should be well under 1 % of one core.
6. **Long session.** 2 h stream with KBM captured; Veyra's private bytes should be flat after the
   first minutes (the engine allocates nothing per event; the stats are bounded).

## Manual test checklist (real hardware required)

Mark each with date, build, result. None of these has been executed for this delivery.

**Capture and safety**
- [ ] Feature off: DualSense behaves exactly as before (buttons, sticks, gyro, touchpad, rumble, adaptive triggers, haptics).
- [ ] Click picture → captured notice; cursor hidden and confined; Veyra shortcuts (Space, F, Esc) do not act on the player.
- [ ] Ctrl+Alt+Shift+Z releases; holding W + LMB during release leaves nothing pressed in game.
- [ ] Alt+Tab, Win key, a toast/notification taking focus, and clicking another monitor each release capture.
- [ ] Unplug the mouse (and separately the keyboard) while holding a key: capture releases, nothing stuck.
- [ ] Windowed, borderless and fullscreen: capture, release, resize while captured (cursor clip follows).
- [ ] Connection drop / PS5 reconnect while captured: no stuck input after recovery; capture ends when the session ends.

**Mapping**
- [ ] Every default binding in all four profiles produces the intended PS5 button (PS5 system UI is fine for buttons).
- [ ] F1–F4 switch profiles; holding W while switching to Driving does not accelerate until W is pressed again.
- [ ] Toggle and repeat behaviours; modifier binding overrides plain binding.
- [ ] Wheel up/down: one D-pad press per notch; fast spin queues at most 8.
- [ ] Weapon wheel (Tab): slot follows mouse direction; release selects; camera does not jerk afterwards.
- [ ] Rebind by keyboard, mouse button and wheel; conflict warning; import/export round trip.
- [ ] Corrupt `keyboard-mouse.txt` by hand: defaults restored, damaged file kept, message shown.

**Aim**
- [ ] Smallest mouse movement moves the camera (anti-dead zone set); camera stops when the mouse stops.
- [ ] ADS (hold RMB) applies the multiplier from the first frame of aiming.
- [ ] Calibration steps 1–3 complete and Apply changes the feel as expected.
- [ ] 125/500/1000/2000 Hz mice feel the same at the same DPI setting.

**Hybrid**
- [ ] DualSense left stick overrides WASD when deflected; triggers combine; DualSense gyro/touch still work.
- [ ] Rumble/adaptive triggers still reach the DualSense while KBM is captured.

**Video and pipeline**
- [ ] Stream video, NR/SR enhancement and frame generation on/off while captured: input unaffected (submit interval unchanged).
- [ ] Submit interval in streaming mode ≈ 4 ms with the high-resolution wait (compare with a build before this change if available).
