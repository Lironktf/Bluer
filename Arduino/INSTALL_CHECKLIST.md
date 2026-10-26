# Install-day checklist: ESP32-S3 combined nodes (SJU)

Goal: one visit, then remote-only maintenance.

## 1. Before leaving home

- [ ] Remote thresholds page and live readings (section 7) built, deployed and home-tested first
- [ ] Every S3 flashed and home-tested on the latest sketch (all new features tested at home first)
- [ ] Arduino Tools for every S3: Board **ESP32S3 Dev Module**, USB CDC On Boot **Enabled**, Flash Size **4MB**,
      Partition Scheme **Minimal SPIFFS (1.9MB APP with OTA)** (keeps remote firmware updates possible later
      without another cable flash)
- [ ] Each node labelled with its pair number (sj-d# / sj-w#) and thresholds from `MACHINES.md` set
- [ ] 10 × good 5 V 3 A wall supplies (one per node), 10 × USB-C cables long enough for a slack loop
- [ ] Tape (section 3), adhesive cable clips, cable ties, "BLUER - DO NOT UNPLUG" labels
- [ ] Anti-vibration pads for between machines that touch (thin neoprene or Sorbothane)
- [ ] Laptop with Arduino IDE + `tools/plot_dryer_washer.py` working, isopropyl alcohol + cloths, multimeter

## 2. Power and cables (the most common failure so far)

- [ ] One supply per node, plugged straight into the wall (no shared hubs, no DC extenders)
- [ ] Cable long enough that with the machine pulled fully out there is still slack
- [ ] With the machine pushed back, the cable sits in a fixed spot: clipped every 20-30 cm, nowhere near moving
      parts, the exhaust, or anything that vibrates against it (that causes false readings)
- [ ] Every connection secured: USB-C clipped next to the board, plug end secured at the outlet
- [ ] Label both ends "BLUER - DO NOT UNPLUG"

## 3. Mounting

**Tape (heat-rated, narrow):**
- MPU board onto the dryer: **3M 467MP adhesive transfer tape**, 1/2" wide. Very thin, so it couples vibration
  tightly; rated to about 149 °C long-term. (Alternative: **3M VHB 4910**, clear, 1/2".)
- Enclosure over it: **3M VHB 5952**, 1/2" wide. Grips painted or slightly textured metal; rated to about 93 °C
  long-term, well above a dryer cabinet's outside temperature.
- How: clean both surfaces with isopropyl alcohol, apply at room temperature (not on a hot dryer), press hard for
  about 30 s. VHB reaches full strength in about 72 h, so mount before the testing day if possible.

**Steps:**
- [ ] Dryer MPU in the same spot on every dryer (as now), bonded flat and tight with 467MP
- [ ] Enclosure taped over it with VHB 5952
- [ ] Mic port (3D design change): the mic comes **out of the print** through an opening or short tube, aimed at
      the washer's water inlet valve, as close as possible. Acoustic mesh over it if lint is an issue.
- [ ] Pads between any machines whose cabinets touch (ask facilities first)
- [ ] Optional, only if clearly needed: magnet mounts instead of tape

## 4. Graph testing (per pair, before leaving that pair)

Use `plot_dryer_washer.py` (or the live readings page, section 7). Write the numbers down for each pair.

**Dryer:**
- [ ] Run this dryer alone: note `DryerAvg10` while running
- [ ] Run each neighbour dryer alone (and the washer under it during spin): note this dryer's `DryerAvg10`
- [ ] Set `DYRERONTHRESHHOLD` between the two, with margin on both sides
- [ ] Open/close the door: `DryerEmpty` flips correctly, and neighbours' doors don't flip it

**Washer:**
- [ ] Run this washer alone: note first-fill peak in both water bands
- [ ] Run each neighbour washer alone: note what this mic hears
- [ ] `FIRST_FILL_HIGH2` sits clearly above what the neighbours cause and clearly below this washer's own fill
- [ ] This washer's `REFILL_HIGH` must stay above the neighbours' first-fill level heard at this mic
- [ ] Refill timing: check the refill really comes about 10 min (`TIMER_REFILL_MS`) after the previous water-in
- [ ] Washer ends at the right time: time a full cycle and compare with when `WasherRunning` drops
- [ ] Look for a clear end-of-cycle signature (for example the final spin, then silence) that is visible on
      every washer and different from the neighbours' ending at the same time. If found, it can replace timers.
- [ ] If thresholds end up too close to separate this washer from its neighbours: piezo contact sensor on the
      inlet hose or valve, and/or acoustic mesh or shroud on the mic

## 5. Before leaving the room

- [ ] Every node shows on `/test` with a fresh "last seen" and sensor "ok" for both MPU and mic
- [ ] Every machine shows the right status on the public site
- [ ] Every cable re-checked: clipped, labelled, slack loop, nothing touching moving parts
- [ ] Tell the service/maintenance staff the nodes are there, and ask them to be careful moving machines

## 6. Out-of-order machines

- Residents can already report a machine broken: 2 reports within an hour mark it broken on the site for 24 h.
- A long-term out-of-order machine would need repeated reports. Consider an admin "out of order" switch later.

## 7. Remote work: build before install day

**Thresholds page (password protected):**
- Shows every node's live thresholds: dryer on-threshold and door change; washer first-fill, refill, refill timer,
  door drop, full and max cycle timers. Also a history of what each one was set to before.
- What is on the page is exactly what the node uses. The node picks up changes in the reply to its normal report
  (on every status change and at least every 5 min), saves them so they survive restarts, and falls back to its
  built-in values if it has never received any. The page shows "applied" once the node confirms it.
- Firmware change needed to accept thresholds from the server.

**Live readings (turned on by a "test" switch on the same page):**
- When the switch is on, the node collects one value per second and uploads a batch every ~10-15 s (one upload per
  second over HTTPS would be too slow on the ESP32).
- Dryer, per second: `avg10` (it only updates once a second anyway), the **biggest jump** of `activitysmall` over
  0.15 s in that second (exactly what the door check compares to the door-change threshold), and running/empty.
- Washer, per second: the **highest** value of each water band in that second (shows whether a threshold was ever
  crossed), the biggest one-step drop of the door band (that drop is what door detection compares to 750,000),
  and running/empty.
- The switch turns itself off after ~60 min so a forgotten test can't run forever. Readings are auto-deleted
  after a few days so the database doesn't fill up.

**Weekly email check:**
- [ ] Script that reviews the week's test history and emails only on a serious issue: a node offline for hours,
      a machine with no cycles for days, repeated reboots, a sensor fault, or two machines copying each other.
- Runs automatically on a schedule (Mac launchd/cron, or a scheduled GitHub Action so the computer doesn't need
  to be on).

**Later:** security (device token on uploads, protected firmware updates), remote firmware updates (OTA).
