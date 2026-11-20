# PS5 keyboard & mouse — troubleshooting

Logs: Veyra's log file, components `ps5-kbm` (capture, persistence), `remoteplay-input` (pad),
`remoteplay-transport` (network window), `remoteplay-recovery` (reconnects). Credentials and pairing
keys are never written to these logs; keyboard/mouse profiles contain no credentials.

| Symptom | Likely cause | What to do |
|---|---|---|
| Clicking the picture does nothing | Feature switch off; no PS5 session; view-only stream; mode "Controller only"; another dialog open | Turn on 键鼠控制, connect (not view-only), choose Hybrid or Keyboard/mouse only, close dialogs |
| "原始输入不可用 / raw input unavailable" | Raw-input window could not be created or registered | Check the log line `ps5-kbm RegisterRawInputDevices failed error=…`; security software that blocks raw input; restart Veyra |
| Capture releases by itself | Foreground changed (notification, overlay, another app stole focus), or a keyboard/mouse was unplugged | Expected safety behaviour; check for overlays that take focus; click the picture again |
| Camera does not move for small mouse movements | Game dead zone larger than the anti-dead zone | Raise anti-dead zone or run calibration step 1 |
| Tiny movements jump too far | Anti-dead zone too high, or a power curve on a linear game | Lower anti-dead zone; curve → Linear |
| Fast flicks feel capped | The game's maximum turn speed (cannot be exceeded) | Lower sensitivity so you reach the cap less often; optional fast-turn carry-over; raise in-game controller sensitivity |
| Turning keeps going briefly after the mouse stops | Smoothing > 0 or fast-turn carry-over > 0 | Set both to 0 |
| Turning speeds up while the stick is held | The game's own turn acceleration (calibration reports "time ramp") | Disable in-game look acceleration if offered; not removable by Veyra |
| Diagonal mouse movement is slower/faster than straight | Game uses an axial or square stick gate | Try dead-zone shape Axial or Diagonal Square |
| Different feel with a different mouse | DPI field does not match the mouse | Enter the mouse's real DPI |
| Mouse works in menus but camera does not move | Active profile is Menus (mouse → left stick) | F1 / F2 / F3, or set "Mouse drives" to Right stick |
| A key seems stuck in game | Toggle behaviour on that binding, or a game action that latches | Press the key again (toggle), check the binding's behaviour; releasing capture always neutralizes everything |
| Weapon wheel selects the wrong slot | Radial sensitivity too high/low | Adjust 轮盘灵敏度 (radial sensitivity); hold Tab, move towards the slot, release |
| Wheel scroll does nothing | Game needs a longer press | Raise "wheel press duration" |
| Both pad and mouse fight over the camera | Pad stick drifting above 0.15 | Use Stick priority = Keyboard/mouse, or Keyboard/mouse only mode |
| "键鼠配置无法读取 / settings could not be read" | Edited or damaged `keyboard-mouse.txt` | Defaults were restored; the damaged file is kept as `keyboard-mouse.corrupt-<time>.txt` next to it |
| Input feels delayed only while streaming | Decode on the session owner thread delaying submissions, or Wi-Fi jitter | Read the live "submit interval" (target 4 ms, max should stay well under 10 ms); try hardware decode; check `remoteplay-transport` packet loss |
| Absolute-position warning in the log | Remote Desktop / tablet / VM pointer | Use a physical relative mouse on the PC itself |

## Network (Wi-Fi to Wi-Fi)

* Keep both devices on the router's 5 GHz or 6 GHz band; avoid 2.4 GHz and mesh hops between rooms.
* Packet loss appears in `remoteplay-transport packetWindowLost=…`; repeated keyframe requests in
  `remoteplay-recovery`. Lower the requested bitrate (Low latency preset) if loss is frequent.
* `RP_IN_USE` after a disconnect: the PS5 keeps the old session ~20 s; Veyra waits 10/20/40 s.

## Reporting a problem

Export your profile (键鼠设置 → 导出当前), copy the last minute of the log, and note: mouse model, DPI,
polling rate, Windows display mode (windowed / borderless / fullscreen), input mode, and the live
panel's submit interval and processing time.
