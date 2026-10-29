# Install-day checklist: SJU nodes

Goal: one visit, then remote-only maintenance.

Choose one hardware path per pair. Shared power, mounting, testing and handover procedures follow both paths.

## A. ESP32-S3: one combined board per pair

Sketch: [dryer_and_washer_bluer.ino](dryer_and_washer_bluer/dryer_and_washer_bluer.ino).
One board reads both sensors and uploads both machines over eduroam; no BLE pairing.

### Upload settings

- [ ] Board: **ESP32S3 Dev Module**; **USB CDC On Boot: Enabled**
- [ ] Flash Size: **4MB only for a board confirmed to have 4MB**; otherwise select its actual flash size
- [ ] Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA)**; this reserves space but does not implement OTA
- [ ] Select the attached board's port and use a USB data cable
- [ ] Put `secrets.h` in this sketch's folder, defining `EAP_USERNAME` and `EAP_PASSWORD` with real eduroam
  credentials. Never commit it, build output or flash dumps
- [ ] Verify/compile in Arduino IDE before uploading, then check boot logs and sensor readings

### What changes per pair N

| Setting | Value |
|---|---|
| `dryerMachineId` | `"sj-dN"` |
| `washerMachineId` | `"sj-wN"` |

Replace N with the machine sticker number, 1-10. These are the required per-pair code changes.
Other `VARIES` threshold lines are fallback defaults: tune the actual pair through `/test/nodes`, using
[MACHINES.md](MACHINES.md) as the starting reference. If offline fallback tuning is required, use that pair's
values, not another pair's values. Credentials and board settings are shared for identical boards.

### Wiring and supplies

- [ ] MPU6050: SDA **8**, SCL **9**, AD0 **GND**, VCC **3V3**, common GND
- [ ] INMP441: SCK **4**, WS **5**, SD **6**, L/R **GND**, VDD **3V3**, common GND
- [ ] For 10 pairs: 10 certified, regulated 5 V supplies and 10 board-compatible USB cables; the existing
  5 V 3 A supply choice is adequate. Keep sensor leads short and verify readings with the final routing
- [ ] Label the enclosure with both machine IDs

## B. Classic ESP32: separate dryer and washer boards

Sketches: [dryer_bluer.ino](dryer_bluer/dryer_bluer.ino) and
[washer_bluer.ino](washer_bluer/washer_bluer.ino). The dryer connects to eduroam and relays the washer's BLE
status and readings; the washer uses BLE only. Flash both boards of each pair together because the older
firmware uses a different BLE format. Use these main sketches, not the nested `*_no_live` versions.

### Upload settings

| Arduino setting | Dryer | Washer |
|---|---|---|
| Board | **ESP32 Dev Module** | **ESP32 Dev Module** |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA)** | **Default** |
| Port | Attached dryer's port | Attached washer's port |

- [ ] The dryer does not fit the default app partition. The OTA partition reserves space only; it does not
  mean remote firmware updates are implemented
- [ ] Leave other board options at the board defaults unless the actual hardware requires otherwise;
  select its real flash size. Do not copy S3 USB CDC settings to classic ESP32 boards
- [ ] Dryer only: put `secrets.h` in the dryer sketch folder with real `EAP_USERNAME` and `EAP_PASSWORD`.
  The washer needs no WiFi credentials. Never commit secrets, build output or flash dumps
- [ ] Verify/compile both in Arduino IDE before uploading. Compilation of the latest refill-mode-3 changes
  has not yet been confirmed; do not treat the earlier hung CLI attempt as a successful build
- [ ] Check boot logs after upload. Avoid erasing all flash unless intentionally clearing saved settings;
  if erased, recheck the website configuration is received and applied

### What changes per pair N

| Sketch | Setting | Value |
|---|---|---|
| Dryer | `machineId` | `"sj-dN"` |
| Dryer | `washerMachineId` | `"sj-wN"` |
| Washer | `PAIR_NUMBER` | `N` |
| Washer | `machineId` | `"WASHER_NODE_WN"` (optional serial label only) |

The dryer derives its BLE pair number from its ID's trailing digits; it must equal the washer's
`PAIR_NUMBER`. No BLE-name filter needs editing in this protocol. The washer is currently set to pair 1:
do not upload that unchanged to all washers.

These `VARIES` identity lines are the only required per-node code changes for identical hardware. Pins,
URLs, credentials, watchdogs and reboot settings remain the same. Per-machine thresholds come from the
website, are relayed through the dryer and are saved across restarts; built-in defaults apply until then.

### Wiring and supplies

- [ ] Dryer MPU6050: SDA **21**, SCL **22**, AD0 **GND**, VCC **3V3**, common GND
- [ ] Washer INMP441: SCK **26**, WS **25**, SD **33**, L/R **GND**, VDD **3V3**, common GND
- [ ] For 10 pairs: power 20 boards independently with certified, regulated 5 V supplies and cables matching
  each board's USB connector (check Micro-USB versus USB-C before buying)
- [ ] Label each enclosure with its machine ID and pair number
- [ ] Confirm the dryer receives its own washer, including mic health, and no neighbouring pair

## Common procedures: both hardware paths

### 1. Before leaving home

- [ ] Remote thresholds and live readings deployed and home-tested; every board compiled, flashed and tested
- [ ] Record existing per-machine settings before flashing; use [MACHINES.md](MACHINES.md) as the registry
- [ ] Open `/test/nodes` for each pair: save if not saved before, otherwise review the existing values.
  Confirm the applied version after both sensors/boards are online; do not overwrite individual tuning
  with generic defaults. Test refill modes 1 (lower band), 2 (either), and 3 (upper band) where needed
- [ ] Pack the supplies/cables for the chosen hardware path, plus spares
- [ ] Tape (section 3), cable clips, cable ties, secondary restraints, "BLUER - DO NOT UNPLUG" labels
- [ ] Anti-vibration pads for machines that touch, only with facilities' approval
- [ ] Laptop with Arduino IDE and live readings working, isopropyl alcohol, lint-free cloths, multimeter,
  soldering/crimp tools and heat-shrink for low-voltage wiring repairs

### 2. Power and cables (the most common failure so far)

- [ ] One supply per node, plugged straight into the wall (no shared hubs, no DC extenders)
- [ ] Cable long enough that with the machine pulled fully out there is still slack
- [ ] With the machine pushed back, the cable sits in a fixed spot: clipped every 20-30 cm, nowhere near moving
      parts, the exhaust, or anything that vibrates against it (that causes false readings)
- [ ] Every USB connection strain-relieved next to the board, with a slack loop; use facilities-approved
  outlet retention that does not obstruct safe unplugging or electrical access
- [ ] Replace loose push-on Dupont jumpers/breadboards with soldered low-voltage joints or properly crimped
  locking connectors. Insulate exposed joints with heat-shrink and gently tug-test every wire
- [ ] Secure the cable jacket to the enclosure so pulling a cable cannot load sensor pins or solder joints
- [ ] No changes to appliance mains wiring, grounding, hoses, vents or safety interlocks
- [ ] Label both ends "BLUER - DO NOT UNPLUG"

### 3. Mounting

**Tape (heat-rated, narrow):**
- MPU board onto the dryer: **3M 467MP adhesive transfer tape**, 1/2" wide. Very thin, so it couples vibration
  tightly. Keep adhesive on an insulated mounting surface or suitable board area, not exposed contacts.
- Enclosure over it: **3M VHB 5952**, 1/2" wide. Grips painted or slightly textured metal; rated to about 93 °C
  long-term, well above a dryer cabinet's outside temperature.
- How: clean both surfaces with isopropyl alcohol, apply at room temperature (not on a hot dryer), press hard for
  about 30 s. VHB reaches full strength in about 72 h, so mount before the testing day if possible.

**Steps:**
- [ ] Dryer MPU in the same spot on every dryer (as now), bonded flat and tight with 467MP
- [ ] Lightweight plastic enclosure secured with VHB 5952; isolate circuit boards from the metal cabinet
- [ ] Add an independent short safety tether to a facilities-approved existing anchor. An adhesive clip
  on the same panel is not an independent backup; do not drill the appliance or use hoses as anchors
- [ ] Keep enclosures and cable restraints off hot surfaces, vents, access panels and moving parts. Verify
  the actual mounting temperature and the chosen adhesive's datasheet limits; no tape guarantees forever
- [ ] Mic port (3D design change): the mic comes **out of the print** through an opening or short tube, aimed at
      the washer's water inlet valve, as close as possible. Acoustic mesh over it if lint is an issue.
- [ ] Pads between any machines whose cabinets touch (ask facilities first)
- [ ] If adhesive cable-tie bases are used, choose rated industrial parts and treat them as strain relief,
      not the sole enclosure restraint. Any magnet alternative needs facilities approval and a safety tether

### 4. Graph testing (per pair, before leaving that pair)

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

### 5. Before leaving the room

- [ ] Every node shows on `/test` with a fresh "last seen" and sensor "ok" for both MPU and mic
- [ ] Every machine shows the right status on the public site
- [ ] Every cable re-checked: clipped, labelled, slack loop, nothing touching moving parts
- [ ] After adhesive dwell, check mounting and strain relief with a gentle tug; repeat after a complete
  operating cycle. Inspect after the first few days and during routine maintenance
- [ ] Tell the service/maintenance staff the nodes are there, and ask them to be careful moving machines

### 6. Out-of-order machines

- Residents can already report a machine broken: 2 reports within an hour mark it broken on the site for 24 h.
- A long-term out-of-order machine would need repeated reports. Consider an admin "out of order" switch later.

### 7. Remote operation and remaining work

**Thresholds page (`/test/nodes`, currently not password protected):**
- Shows every node's live thresholds: dryer on-threshold and door change; washer first-fill, refill, refill timer,
  door drop, full and max cycle timers. Also a history of what each one was set to before.
- What is on the page is exactly what the node uses. The node picks up changes in the reply to its normal report
  (on every status change and at least every 5 min), saves them so they survive restarts, and falls back to its
  built-in values if it has never received any. The page shows "applied" once the node confirms it.
- Both hardware paths support these settings. With two classic ESP32s, the dryer forwards washer settings
  over BLE; check the applied acknowledgement, not just whether the website saved the values.

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
- With no new samples the graph anchors to the last sample, not the clock. It is not a permanent snapshot:
  the page fetches only the last 15 minutes, so old readings eventually disappear.

**Automatic recovery:**
- Both classic ESP32 sketches schedule an idle restart after 24-30 hours and a 7-day absolute restart cap.
  An active cycle can defer the idle restart. Watchdogs and sensor recovery are additional backstops,
  not a guarantee of unattended operation or a replacement for mounting inspections.

**Weekly email check:**
- [ ] Script that reviews the week's test history and emails only on a serious issue: a node offline for hours,
      a machine with no cycles for days, repeated reboots, a sensor fault, or two machines copying each other.
- Runs automatically on a schedule (Mac launchd/cron, or a scheduled GitHub Action so the computer doesn't need
  to be on).

**Later:** security (device token on uploads, protected firmware updates), remote firmware updates (OTA).
