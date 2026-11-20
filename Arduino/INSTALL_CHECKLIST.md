# Install-day checklist: SJU nodes

Goal: one visit, then remote-only maintenance.

Choose one hardware path per pair. Shared power, mounting, testing and handover procedures follow both paths.

## A. ESP32-S3: one combined board per pair

Sketch: [dryer_and_washer_bluer.ino](dryer_and_washer_bluer/dryer_and_washer_bluer.ino).
One board reads both sensors and uploads both machines over the open "UniversityOfWaterloo" Wi-Fi, with its MAC
on the IST allow list like the dryers (section B); no BLE pairing. Its Wi-Fi, reports, live readings and
automatic recovery work exactly like the classic dryer's (section 7).

### Upload settings

- [ ] Board: **ESP32S3 Dev Module**; **USB CDC On Boot: Enabled**
- [ ] Flash Size: **4MB only for a board confirmed to have 4MB**; otherwise select its actual flash size
- [ ] Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA)**; this reserves space but does not implement OTA
- [ ] Select the attached board's port and use a USB data cable
- [ ] Board's MAC on the UniversityOfWaterloo allow list (see section B) before flashing
- [ ] Verify/compile in Arduino IDE before uploading, then check boot logs and sensor readings

### What changes per pair N

| Setting | Value |
|---|---|
| `dryerMachineId` | `"sj-dN"` |
| `washerMachineId` | `"sj-wN"` |

Replace N with the machine sticker number, 1-10. These are the required per-pair code changes.
Other `VARIES` threshold lines are fallback defaults: tune the actual pair through `/test/nodes`, using
[MACHINES.md](MACHINES.md) as the starting reference. If offline fallback tuning is required, use that pair's
values, not another pair's values. Board settings are shared for identical boards.

### Wiring and supplies

- [ ] MPU6050: SDA **8**, SCL **9**, AD0 **GND**, VCC **3V3**, common GND
- [ ] INMP441: SCK **4**, WS **5**, SD **6**, L/R **GND**, VDD **3V3**, common GND
- [ ] For 10 pairs: 10 certified, regulated 5 V supplies and 10 board-compatible USB cables; the existing
  5 V 3 A supply choice is adequate. Keep sensor leads short and verify readings with the final routing
- [ ] Label the enclosure with both machine IDs

## B. Classic ESP32: separate dryer and washer boards

Sketches: [dryer_bluer.ino](dryer_bluer/dryer_bluer.ino) and
[washer_bluer.ino](washer_bluer/washer_bluer.ino). The dryer connects to the open "UniversityOfWaterloo"
Wi-Fi and relays the washer's BLE
status and readings; the washer uses BLE only. Flash both boards of each pair together because the older
firmware uses a different BLE format. Use these main sketches, not the nested `*_no_live` versions.

### Upload settings

| Arduino setting | Dryer | Washer |
|---|---|---|
| Board | **ESP32 Dev Module** | **ESP32 Dev Module** |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA)** | **Default** |
| Port | Attached dryer's port | Attached washer's port |

- [ ] Flash Mode / Flash Frequency: leave at the board defaults. The partition scheme is the setting that matters
- [ ] **Wi-Fi allow list (before flashing).** The dryers no longer use eduroam. They join the open
  "UniversityOfWaterloo" network, which only lets in MAC addresses IST has allow-listed. Read every dryer
  board's MAC and every spare's (`esptool.exe --port COMx read-mac`, or the `MAC` line the dryer prints at boot
  and that an upload prints), then send them through the IST Service Desk online portal live chat (or email
  helpdesk@uwaterloo.ca). In the Nov 18 chat, IST said this usually takes about 2 minutes and is easy to do.
  Washers need no allow-listing (BLE only). A replacement board needs the same request with its MAC. Flash only
  once IST confirms the addresses are added. "UniversityOfWaterloo" is the confirmed network name in the SJU
  laundry room.
  Allow-list processing may be quick, but devices can take 30-60 minutes to connect successfully afterward;
  occasionally it can take up to 24 hours. Leave them powered and let them retry. If they are still offline
  after 24 hours, confirm the MACs and network name with IST before reflashing.
  A board that joins but can't report at first usually recovers by itself within minutes: leave it alone.

  Example live-chat request (update the device list as you collect more MACs):

  > Hello, I had a live chat earlier today with Elias about the laundry solution at SJU. Please add these
  > devices to the UniversityOfWaterloo wireless Wi-Fi allow list for one year.
  >
  > I was told to provide this information:
  >
  > Name: Oliver Simser
  > WatIAM username: osimser
  > Devices (laundry sensors, SJU laundry room):
  > Device 1: 70:4b:ca:99:62:a0
  > Device 2: b4:bf:e9:05:01:14
- [ ] **Renew the allow list before it expires** (1 year max). Put a reminder in your calendar a couple of weeks
  before. If it lapses the dryers go offline, keep retrying and resetting every 5 min, and come back by
  themselves within about 5 min of being re-added; no reflash needed
- [ ] The dryer does not fit the default app partition. The OTA partition reserves space only; it does not
  mean remote firmware updates are implemented
- [ ] Leave other board options at the board defaults unless the actual hardware requires otherwise;
  select its real flash size. Do not copy S3 USB CDC settings to classic ESP32 boards
- [ ] The washer needs no Wi-Fi settings. Never commit build output or flash dumps
- [ ] Verify/compile both in Arduino IDE before uploading
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
- [ ] **Power, one supply per pair** (the tested setup): one 5 V 2 A wall adapter powers only that pair's dryer
  and its own washer:
  wall adapter (5.5x2.1 mm plug) -> 5.5x2.1 female to 5.5x2.5 male adapter -> 1-to-2 splitter (5.5x2.5 mm)
  -> two 1.5 m DC extension cables (5.5x2.5 mm) -> DC to USB-C adapters -> dryer board and washer board.
  Never share a supply between pairs: on a 4-way splitter feeding two pairs, dryer 2 crashed and froze
  repeatedly (Nov 18); split two ways with only its washer, it ran all night with no restarts (Nov 19-20)
- [ ] Label each enclosure with its machine ID and pair number
- [ ] Confirm the dryer receives its own washer, including mic health, and no neighbouring pair

## Common procedures: both hardware paths

### 1. Before leaving home

- [ ] Remote thresholds and live readings deployed and home-tested; every board compiled, flashed and tested
- [ ] Test the 5-min full reset (section 7) on the home-test build: turn home Wi-Fi off once it reports, and
  check the Serial Monitor shows `[SUPERVISOR] Full reset` after 5 min and it reconnects and reports (reset
  reason 8) when Wi-Fi is back
- [ ] Test Wi-Fi recovery on the home-test build: turn the router off for 1 min, then on. The node should
  print "Not connected. Restarting Wi-Fi..." and report again within about a minute of the router being back
- [ ] Record existing per-machine settings before flashing; use [MACHINES.md](MACHINES.md) as the registry
- [ ] Open `/test/nodes` for each pair: save if not saved before, otherwise review the existing values.
  Confirm the applied version after both sensors/boards are online; do not overwrite individual tuning
  with generic defaults. Test refill modes 1 (lower band), 2 (either), and 3 (upper band) where needed
- [ ] Pack one power set per pair (section B "Wiring and supplies": 5 V 2 A adapter, 2.1-to-2.5 adapter,
  1-to-2 splitter, two extension cables, two DC to USB-C adapters), plus spares of each part
- [ ] Tape (section 3), cable clips, cable ties, secondary restraints, "BLUER - DO NOT UNPLUG" labels
- [ ] Foam between machines (section 3), only with facilities' approval. Cheapest option: a roll of
  self-adhesive foam or EPDM rubber weatherstripping (hardware store door/window aisle, about 1/4" thick)
- [ ] Spare INMP441 mics, for swapping out any that read low in the baseline test (section 4)
- [ ] iPhone with the artificial water-in video, for the mic baseline test (section 4)
- [ ] Laptop with Arduino IDE and live readings working, isopropyl alcohol, lint-free cloths, multimeter,
  soldering/crimp tools and heat-shrink for low-voltage wiring repairs

### 2. Power and cables (the most common failure so far)

- [ ] One 5 V 2 A wall adapter per pair, split two ways to that pair's dryer and washer only, as in section B
  "Wiring and supplies". No 4-way splitters and no supply shared between pairs
- [ ] Cable long enough that with the machine pulled fully out there is still slack
- [ ] With the machine pushed back, the cable sits in a fixed spot: clipped every 20-30 cm, nowhere near moving
      parts, the exhaust, or anything that vibrates against it (that causes false readings)
- [ ] Every USB connection strain-relieved next to the board, with a slack loop; use facilities-approved
  outlet retention that does not obstruct safe unplugging or electrical access
- [ ] Replace loose push-on Dupont jumpers/breadboards with soldered low-voltage joints or properly crimped
  locking connectors. Insulate exposed joints with heat-shrink and gently tug-test every wire
- [ ] Secure the cable jacket to the enclosure so pulling a cable cannot load sensor pins or solder joints
- [ ] No changes to appliance mains wiring, grounding, hoses, vents or safety interlocks
- [ ] Tape over every power connection (wall plug into the outlet, cable into the plug, cable into the board)
  so no cable can work loose under its own weight
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
- [ ] Foam between every pair of machines whose cabinets touch or nearly touch (ask facilities first), so one
  machine's vibration can't set off its neighbour's sensor. Stick self-adhesive foam strips on one machine's
  side panel near the top and bottom corners, after cleaning with isopropyl alcohol. If the foam isn't
  self-adhesive, hold each strip with short pieces of the same tape along its top and bottom edges only, so it
  still squashes between the cabinets. Keep it clear of vents, hoses and access panels
- [ ] If adhesive cable-tie bases are used, choose rated industrial parts and treat them as strain relief,
      not the sole enclosure restraint. Any magnet alternative needs facilities approval and a safety tether

### 4. Graph testing (per pair, before leaving that pair)

Use the live readings page (`/test/live`, section 7). Write the numbers down for each pair.

**Testing day:**
- [ ] Right after flashing each node, start a recording for that pair on `/test/live`
- [ ] Turn all the machines on, and write down when each one started and when its display says it will end.
  Later, run `Arduino/tools/snapshot_recording.py` on each pair's recording CSV and compare its start and end
  snapshots with these notes

**Mic baseline (every washer, when its node is refreshed):**
- [ ] Play the same artificial water-in video from the iPhone at the same distance from the mic and at the
  same volume, and note the peak of both water bands on `/test/live`
- [ ] Every mic should reach similar numbers. Replace any mic that reads clearly low with a spare that day.
  Then, if one washer hears a neighbour much more than the others do, that's the machines' own noise, not
  the mic, so tune the thresholds around it

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
- [ ] **Nov 20 visit: quick cycles.** Run every washer on its quick cycle with recording on (`/test/live`), noting
      start times. Check whether its lower first-fill peak can be caught by lowering the first-fill threshold
      without the neighbouring washers' fills or noise triggering it. If it can't, keep the current threshold:
      quick cycles will then show as ended early on the site, which is acceptable

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

**Thresholds page (`/test/nodes`; it and `/test/live` need `NODE_ADMIN_PASSWORD`, remembered for 7 days per browser):**
- Shows every node's live thresholds: dryer on-threshold and door change; washer first-fill, refill, refill timer,
  door drop, full and max cycle timers. Also a history of what each one was set to before.
- What is on the page is exactly what the node uses. The node picks up changes in the reply to its next report
  (within about 30 s), saves them so they survive restarts, and falls back to its
  built-in values if it has never received any. The page shows "applied" once the node confirms it.
- Both hardware paths support these settings. With two classic ESP32s, the dryer forwards washer settings
  over BLE; check the applied acknowledgement, not just whether the website saved the values.

**Reports and live readings (always on, both hardware paths):**
- The dryer sends one report every 30 s, and sooner (at most every 5 s) when either machine's running/empty
  changes. Each report carries both machines' status for the website and every 5 s reading since the last one.
  At 30 s, 10 nodes use about 93% of Vercel Hobby's 1M function calls a month. Check Vercel's Usage page once
  all 10 run; if it's close, raise `REPORT_EVERY_S` (60 s halves it; no reflash, readings are still all sent).
- Every 5 s, the dryer keeps the highest `avg10` and the biggest 0.15 s door jump; the washer keeps the highest
  value of each water band and the biggest door-band drop. Both keep running/empty at the end of the 5 s.
- The washer broadcasts its last two 5 s rounds, so the dryer can miss Bluetooth for several seconds without
  losing a reading. Washer points may sit up to ~5 s off the dryer's on the graph.
- A failed report is retried after 5, 10, 20... up to 60 s, with everything still queued (up to 10 min).
  A retry replaces, never duplicates, what the server already stored. Readings stay queued on any failure.
- A machine shows offline after 3 missed reports (2 min). The interval is `REPORT_EVERY_S` in
  `dashboard/api/diagnostics.js`: changing it needs a deploy, not a reflash.
- Readings are auto-deleted after 3 days. The graph shows 2.5 min at a time, with a slider back through the
  last 10 min; slid fully right it follows new readings.

**Why the dryers use open campus Wi-Fi instead of eduroam (Nov 17-18, 2026):**
- eduroam needs a personal login on every board. All 10 shared one account, and freshly restarted boards were
  intermittently refused (reason 23) for 10-30 min at a time while the others stayed on. The login also needs a
  large block of memory, which caused the earlier 23/202 failures, and an outside server we couldn't see into.
- Instead of more firmware workarounds (a second account, slower retries), I asked the network owner. IST
  (live chat, Nov 18) recommended the "UniversityOfWaterloo" network with the boards' MACs on an allow list.
- Result: no login server that can refuse the boards, no personal password stored on them, about 100 KB less
  firmware, less memory used, and simpler Wi-Fi code. If the allow list lapses, the boards recover by
  themselves once re-added.

**Full Wi-Fi transmit power (Nov 19-20, 2026):**
- The dryers see the network at only about -77 dBm. They used to transmit at 11 dBm to limit current spikes on
  shared supplies; now they use the maximum, 19.5 dBm. The same night, dryer 2 at full power went silent for
  2 min or more about 4 times, against 19 for dryer 1 at 11 dBm.
- Full power draws more current in short bursts, which a 5 V 2 A supply split between one dryer and its washer
  handles fine. Gaps that remain are mostly the busy residence network (about 10 pm to 1:30 am): readings stay
  queued and fill in afterwards, and the 5 min reset ends long ones.

**Automatic recovery** (the classic dryer and the ESP32-S3 combined board work the same way):
- Every sketch schedules an idle restart after 24-30 hours and a 7-day absolute restart cap.
  An active cycle can defer the idle restart. Watchdogs and sensor recovery are additional backstops,
  not a guarantee of unattended operation or a replacement for mounting inspections.
- The dryer first retries on its own: the Wi-Fi library reconnects straight away after a drop; if that hasn't
  worked, the dryer restarts Wi-Fi from scratch after 30 s, then 1, 2, 4, and every 5 min. It also re-joins
  after 5 failed reports. These restart only the Wi-Fi, so a fault in the rest
  of the chip survives them.
- Last resort, added after node 2 stayed powered but silent for 4 days in Nov 2026 until it was unplugged: if no
  report has reached the website for 5 min, the dryer fully resets itself with a 1 s deep sleep. That powers
  down the radio and most of the chip, the closest software gets to unplugging it, and it doesn't wait for the
  dryer to be idle. On `/test` the reset reason shows as "deep sleep (8)". During a Wi-Fi or website outage, or
  while off the allow list, nodes reset every 5 min until it's back, which is harmless. Only a real reply from
  the website counts, so a Wi-Fi sign-in page can't stop the resets.
- **What isn't covered:** a board whose hardware is genuinely failing, so that even the deep reset doesn't help
  and only cutting power does. If that happens you'll see "deep sleep (8)" resets every 5 min on `/test` with
  no reports between them. Check power first (section 2): dryer 2's crashes and freezes in Nov 2026 were its
  shared 4-way supply. If it continues on the standard per-pair power, swap the board.

**Weekly email check:**
- [ ] Script that reviews the week's test history and emails only on a serious issue: a node offline for hours,
      a machine with no cycles for days, repeated reboots, a sensor fault, or two machines copying each other.
- Runs automatically on a schedule (Mac launchd/cron, or a scheduled GitHub Action so the computer doesn't need
  to be on).

**Later:** security (device token on uploads, protected firmware updates), remote firmware updates (OTA).
