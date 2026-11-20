# PS5 keyboard & mouse — installation and configuration guide

This feature turns your keyboard and mouse into a **virtual DualSense** for PS5 Remote Play inside
Veyra. Read the first section before tuning: it explains what is and is not possible.

## 1. What to expect

* PS5 games only see a controller. The mouse drives the **right stick**; the game decides how fast the
  camera turns for each stick position and caps the maximum speed. Small, precise corrections and
  instant stops work well. Very fast flicks are limited by the game's maximum turn speed — no
  software can exceed it.
* The game's own aim assist, aim smoothing and turn acceleration still apply.
* Rumble, adaptive triggers, haptics, gyro and the touchpad come from a physical DualSense if one is
  connected (Hybrid mode). Keyboard/mouse alone has no rumble.
* A DualSense (or the PS5 itself) is still needed for: first pairing and some console prompts, and
  anything that requires the real PS button long-press, mic, or the speaker. The PS button is mapped
  to **F10** by default, Create to **Backspace**, the touchpad click to **M** (map).
* The shipped GTA bindings are modelled on the **GTA V PlayStation layout**. They are an assumption,
  not a verified GTA VI layout. Check the game's controller settings and edit the bindings.

## 2. Requirements

* Windows 11, a Veyra build with PS5 Remote Play enabled, a paired PS5 (PS5 dialog → pairing).
* A **relative** mouse. Remote Desktop, some tablets and VMs send absolute positions, which cannot
  turn a camera; Veyra logs a warning and ignores them.
* Recommended for the target setup (Wi-Fi 7 router, both devices on Wi-Fi): PC and PS5 on the same
  5/6 GHz network, not a 2.4 GHz band, and the PS5 close to the router. Wired Ethernet on either side
  removes the largest source of jitter.

## 3. First-time setup

1. Open **PS5 串流 (PS5 stream)** and connect as usual. Optionally pick a **stream preset**:
   *Low latency* (H.264, hardware decode, 20 Mbps), *Balanced* (H.265, 20 Mbps), *Quality first*
   (H.265, 50 Mbps). These are reasoned starting points, **not measured results**; the PS5 decides
   the actual bitrate.
2. In the PS5 dialog click **键鼠设置 (Keyboard & mouse settings)**.
3. Turn on **键鼠控制 (Keyboard & mouse)**. Off = Veyra behaves exactly as before.
4. Choose **输入来源 (Input source)**:
   * **Hybrid** (default): keyboard/mouse and the DualSense merge into one controller state. Each stick
     comes from one source per update (the more deflected one by default), so you can pick up the
     DualSense to drive at any time. Buttons from both are combined.
   * **Keyboard/mouse only**: the DualSense is ignored while captured.
   * **Controller only**: keyboard/mouse never reaches the PS5.
5. Enter your mouse's real **DPI** (the value set in its software). Sensitivity is then
   hardware-independent: 800 DPI at sensitivity 1.0 feels the same as 3200 DPI at 1.0.
6. Close the dialog with **关闭并捕获键鼠 (Close and capture)**, or click the stream picture.

While captured the cursor is hidden and kept in the window, and Veyra's own shortcuts are off.

**Release:** **Ctrl+Alt+Shift+Z**, or Alt+Tab / Win key / clicking another window. Release always
returns every simulated button, trigger and stick to neutral. Click the picture to capture again.

## 4. Default profiles (switch in game with F1–F4)

Switching profiles first releases everything; keys you are still holding are ignored until you
release them, so switching never produces an unintended press.

### F1 · On foot

| Action (assumed GTA layout) | Key | PS5 control |
|---|---|---|
| Move | W A S D | Left stick (circular diagonals) |
| Walk (slow movement) | Left Alt (hold) | Left stick × 0.5 |
| Camera | Mouse | Right stick |
| Aim | Right mouse | L2 (enables ADS sensitivity) |
| Shoot | Left mouse | R2 |
| Sprint | Left Shift | Cross |
| Jump | Space | Square |
| Reload | R | Circle |
| Interact | E | D-pad Right |
| Enter/exit vehicle | F | Triangle |
| Crouch / stealth | Left Ctrl | L3 |
| Map | M | Touchpad click |
| Weapon wheel | Tab (hold; mouse points at the slot) | L1 + right stick position |
| Pause | Esc | Options |
| Cover | Q | R1 |
| Look behind | C, middle mouse | R3 |
| Phone | T | D-pad Up |
| D-pad Down | B | D-pad Down |
| Cycle (assumed) | Wheel up / down | D-pad Left / Right |
| Create (Share) | Backspace | Create |
| PS button | F10 | PS |

### F2 · Combat
On foot plus: ADS multiplier 0.5, **mouse side button 5 = precision aim** (× 0.4 while held),
**side button 4 = R1**. Smoothing and overflow carry off.

### F3 · Driving
W = **R2** throttle, S = **L2** brake/reverse (trigger ramp 60 ms), A/D = steering with **gradual
steering** (rise 110 ms, return 70 ms, counter-steer passes through centre), Left Alt = gentle
steering (× 0.4), Space = R1 handbrake, F = Triangle exit, E = L3 horn, C = R3 look behind,
right mouse = L1 (drive-by aim, assumed), left mouse = R1, Q / H = D-pad Left / Right (radio, assumed),
arrow up/down = left stick up/down (bikes, aircraft). Mouse = camera at sensitivity 0.7. ADS
multiplier is not tied to L2 here because L2 is the brake.

For analog driving, keep **Hybrid** and simply use the DualSense sticks and triggers — the larger
deflection wins per stick, and triggers take the maximum of both.

### F4 · Menus
Mouse = **left stick cursor** (map, menus), left click = Cross, right click / Esc / Backspace = Circle,
WASD / arrows / wheel = D-pad, Enter / Space = Cross, Q/E = L1/R1 tabs, Z/C = L2/R2, Tab = Triangle,
X = Square, I/J/K/L = right stick, P = Options, M = Touchpad.

## 5. Editing bindings

In **按键 (Bindings)**: **改键 (Rebind)** then press a key, or click a mouse button / scroll in the
highlighted box. **+组合 (+Modifier)** adds a key that must also be held (e.g. Left Ctrl + F); a
modified binding overrides the plain binding of the same key while the modifier is held. Each row
has a target and a behaviour:

* **按住 Hold** — pressed while held.
* **切换 Toggle** — press once on, again off (reset on release of capture and profile switch).
* **连发 Repeat** — pulses 1–30 Hz while held (e.g. tap-to-sprint games).

Conflicts (one key driving two actions) are listed in red at the top. Profiles can be created,
duplicated, renamed, reset to their shipped defaults, deleted, imported and exported as plain text
files (no PSN or pairing data inside). Import appends; it never overwrites existing profiles.

## 6. Tuning mouse aim

Work top-down; change one thing at a time.

1. **Anti-dead zone.** If small mouse movements do nothing, the game's dead zone is swallowing them.
   Raise it until the smallest movement moves the camera. Too high = the slowest movements turn too
   fast and fine aim feels jumpy (the camera still stops the moment the mouse stops). Measure it with
   calibration step 1.
2. **Sensitivity.** 1.0 means about 5 inches/s of hand speed reaches the game's maximum turn speed.
   Lower it until fast flicks no longer feel "stuck"; raise it if you run out of mousepad.
3. **Response curve.** Many games square the stick (slow centre, fast edge). *Power* with exponent
   ~1.5–2.5 or a calibrated *table* makes the turn rate track mouse speed linearly.
4. **ADS multiplier** for aiming, **precision multiplier** for a held sniper key.
5. **Maximum deflection.** If calibration shows the game stops speeding up at, say, 0.85, set it
   there — pushing further only adds dead travel.
6. **Smoothing** stays at 0 unless you want a softer image; it delays stopping.
7. **Fast-turn carry-over** (optional): mouse movement above the maximum turn speed keeps turning for
   up to the set time instead of being lost. 0 gives the crispest stop.

The live panel shows mouse event rate, your mouse's report interval, processing time per update,
the interval between controller submissions, how often the mouse asked for more than the maximum,
and both sticks.

## 7. Guided calibration

Stand somewhere in the game where the camera can turn freely; don't touch other inputs.
Capture must be off (Veyra drives the stick itself). **Space** marks, **Esc** cancels.

1. **Dead zone** — *Start*. Veyra slowly pushes the right stick right. Press *It started turning*
   (or Space) the instant the camera moves. About 220 ms of reaction time is subtracted.
2. **Turn rate** — pick a deflection (0.3 … 1.0), *Start turning*, and mark every time the same
   landmark passes the crosshair, at least three times, then *Record*. Repeat for at least three
   deflections (more is better; include 1.0).
3. **Generate** → shows dead zone, maximum °/s, the saturation point and whether the game speeds up
   over time (a ramp that cannot be removed). **Apply to current profile** writes the anti-dead zone,
   a calibrated curve table and the maximum deflection.
4. **Linearity check** — press *One slow turn*, close the dialog, capture, turn exactly one full circle
   slowly with the mouse, release (Ctrl+Alt+Shift+Z). Repeat with *One fast turn*. Equal mouse
   distance = linear; more distance when fast = you hit the turn-rate ceiling.

## 8. GTA VI notes

* Set the in-game **controller** sensitivity high and dead zone to the minimum the game allows, turn
  off in-game aim acceleration if offered, then calibrate in Veyra — it is easier to linearize a
  fast, simple game curve than a slow, curved one.
* Aim assist is decided by the game for controller input; Veyra cannot turn it on or off.
* Check every default binding against the game's controller layout screen and adjust.

## 9. Video, enhancement and frame generation with keyboard/mouse

Input is sampled at controller submission time on the Remote Play session thread; it does not pass
through the renderer, neural enhancement or frame generation, so enabling those does not delay the
controller state that is sent. They can, however, add **display** latency (frame generation in
particular holds frames to interpolate). For aiming, prefer frame generation off or 2×, and keep the
existing presentation defaults. Generated frames are never counted as game FPS — Veyra shows stream,
decoded, content and presented rates separately.
