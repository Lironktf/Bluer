# MachineWebsite — laundry room monitor

Live washer/dryer availability for the St Jerome's laundry room (SJU-Sieg/Ryan), University of Waterloo.

## How it fits together

- **Dryer node** (`Arduino/dryer_bluer/dryer_bluer.ino`): ESP32 + MPU accelerometer on each dryer.
  Running = 10 s average vibration `avg10 > DYRERONTHRESHHOLD`. Empty = door jolt
  (`activitysmall` jumps by more than `DOORCLOSINGCHANGE`). It joins eduroam and POSTs
  `{machineId, running, empty, sensorState, ...}` to `https://laun-dryer.vercel.app/api/machines`.
- **Washer node** (`Arduino/washer_bluer/washer_bluer.ino`): ESP32 + INMP441 mic, FFT on water-fill
  frequency bands, cycle timers. It has no Wi-Fi: it BLE-advertises `running/empty/micOk` under
  `WASHER_BLE_NAME`, and its paired dryer relays that to the API as the washer's id.
- **Dashboard / API** (`dashboard/`, Vercel + MongoDB): `api/machines.js` stores state;
  `src/utils/machineLabel.js` defines the id scheme.
- Old/reference sketches: `Arduino/old_*.ino`, `very_old_*`, `power_optimized_*` (history only).

## IDs and pairing

- `sj-d<n>` / `sj-w<n>`, n = number on the machine's sticker. Washer n pairs with dryer n.
- BLE name for pair n is `WASHER_W<n>`; it must match in the washer sketch (`WASHER_BLE_NAME`)
  and the dryer sketch (`getName() != "WASHER_W<n>"`).
- Old scheme `a1-m1..a1-m20`: odd = washer, even = dryer, numbered the opposite way:
  old pair k (a1-m(2k-1) / a1-m(2k)) -> new pair 11-k. Old BLE names were `WASHER_A<k>`.

## Thresholds are per device

The old boards were tuned individually (e.g. sj-d10 door 0.55 vs 0.65 elsewhere; sj-w10 refill
51500 on one band vs sj-w7 32000 on both). There is one sketch per node type, re-edited for each
board before flashing. **`Arduino/MACHINES.md` is the registry** of every board's old and new values.

## Workflow when a board is attached — ALWAYS read before configuring

1. **Read the board's current firmware first, before editing any sketch**, even if only asked to
   "configure" it. Flashing destroys the old values forever.
   - Dryer: `/opt/homebrew/opt/esptool/libexec/bin/python Arduino/tools/read_device_config.py --port /dev/cu.usbserial-0001 --json Arduino/device_reads/sj-d<n>_old.json` (~20 s, read-only).
   - Washer: the tool does not decode washers yet. Read the app's DROM (strings) and the first
     0x8000 bytes of IROM, decode the sketch literal pool (doubles are high words, low word 0),
     and confirm REFILL_HIGH (and how many bands it is compared on), TIMER_REFILL_MS and the door
     drop by disassembling `loop()` with `~/Library/Arduino15/packages/esp32/tools/esp-x32/*/bin/xtensa-esp32-elf-objdump`.
     Save the result as `Arduino/device_reads/sj-w<n>_old.json`.
   - Baud: 230400. 460800+ corrupts reads on these USB adapters. If the port is busy, the Arduino
     IDE Serial Monitor probably has it.
2. Check the old ids/BLE name match the expected pair (old pair k = 11 - n).
3. Set the sketch to the new ids/BLE name **and that board's own old thresholds**.
4. Update `Arduino/MACHINES.md` (old vs new table, MAC, source JSON/dump, flash status) and the
   JSON in `Arduino/device_reads/`.
5. The operator flashes from the Arduino IDE (partition scheme Huge APP). Mark "flashed" in
   MACHINES.md only once they confirm.

## Rules

- Never commit `*.bin` flash dumps: they contain the eduroam password in plaintext. Never print
  credentials from a dump. `secrets.h` is gitignored.
- Don't change detection logic when only reconfiguring a board; only ids, BLE name and thresholds.
- Git commit messages must not mention Claude.
