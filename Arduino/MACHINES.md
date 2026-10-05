# Machine registry

Per-device IDs, BLE names and detection thresholds, including what each board ran
before it was reflashed. Update this whenever a board is read back or reflashed.

Naming: `sj-d<n>` / `sj-w<n>`, n = sticker number (see `dashboard/src/utils/machineLabel.js`).
Old scheme `a1-m1..a1-m20`: odd = washer, even = dryer, numbered the opposite way, so old pair k -> new pair 11-k.
BLE name for pair n is `WASHER_W<n>`, set in both the washer sketch and the paired dryer's filter.

## Quick reference: thresholds to flash

Baseline is the generic sketch: dryer `2.45` / `0.65`; washer `TIMER_REFILL_MS 600000`, `FIRST_FILL_HIGH2 68500`,
`REFILL_HIGH 32000` (either band), door drop `750000`. **Bold** = differs from baseline.

| Machine | Old id / old BLE name | Thresholds | Source |
|---|---|---|---|
| sj-d1 | a1-m20 | 2.45 / 0.65 | operator: same as sketch |
| sj-d2 | a1-m18 | 2.45 / 0.65 | operator |
| sj-d3 | a1-m16 | 2.45 / 0.65 | operator |
| sj-d4 | a1-m14 / WASHER_A7 | 2.45 / **0.55** | read-back (operator note says only sj-d10 differs; recheck) |
| sj-d5 | a1-m12 / WASHER_A6 | 2.45 / 0.65 | read-back |
| sj-d6 | a1-m10 / WASHER_A5 | 2.45 / 0.65 | read-back |
| sj-d7 | a1-m8 / WASHER_A4 | 2.45 / 0.65 | read-back |
| sj-d8 | a1-m6 / WASHER_A3 | 2.45 / 0.65 | read-back |
| sj-d9 | a1-m4 / WASHER_A2 | 2.45 / 0.65 | read-back |
| sj-d10 | a1-m2 / WASHER_A1 | 2.45 / **0.55** | read-back + operator |
| sj-w1 | a1-m19 | baseline | operator: same as sketch |
| sj-w2 | a1-m17 / WASHER_A9 | baseline | read-back |
| sj-w3 | a1-m15 / WASHER_A8 | **TIMER_REFILL_MS 630000**, rest baseline | read-back |
| sj-w4 | a1-m13 | **TIMER_REFILL_MS 630000**, rest baseline | operator |
| sj-w5 | a1-m11 / WASHER_A6 | **TIMER_REFILL_MS 630000**, rest baseline | read-back |
| sj-w6 | a1-m9 | **TIMER_REFILL_MS 630000**, rest baseline | operator |
| sj-w7 | a1-m7 / WASHER_A4 | **TIMER_REFILL_MS 630000**, **door drop 550000**, rest baseline | read-back |
| sj-w8 | a1-m5 | **TIMER_REFILL_MS 630000**, rest baseline | operator |
| sj-w9 | a1-m3 | baseline | operator |
| sj-w10 | a1-m1 / WASHER_A1 | **TIMER_REFILL_MS 630000**, **REFILL_HIGH 51500 (7875–9750 band only)**, rest baseline | read-back |

"Operator" rows were reported by hand and only checked the values that differ; "read-back" rows were decoded
from the board's old firmware (details per pair below).

## Pair 10 (sj-d10 / sj-w10, formerly a1-m2 / a1-m1)

### Dryer sj-d10 — ESP32 MAC 30:76:f5:f0:50:98

| Setting | Old firmware (read back) | New values (flashed) |
|---|---|---|
| machineId | a1-m2 | sj-d10 |
| washerMachineId | a1-m1 | sj-w10 |
| Washer BLE filter | WASHER_A1 | WASHER_W10 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | 0.55 | 0.55 |
| WINDOW / WINDOWTWO / WINDOWTHREE | 20 / 10 / 4 | 20 / 10 / 4 |

- Old firmware: `old_dryer_bluer.ino` variant, built Dec 18 2025. Read 2026-10-03 from full dump
  `dryer_bluer/a1-m2.bin` (sha256 `3036e86c56f06c2a4fb2ab32ca3131cfa412110cde8d13be5f34907aa18018fc`), `device_reads/sj-d10_old.json`.
- Flashed with the new sketch 2026-10-03 (upload log: all regions "Hash of data verified").

### Washer sj-w10 — ESP32 MAC 30:76:f5:f0:4f:f0

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| BLE name | WASHER_A1 | WASHER_W10 |
| FIRST_FILL_HIGH2 (either band) | 68500 | 68500 |
| REFILL_HIGH | 51500, 7875–9750 band only | 51500, 7875–9750 band only |
| TIMER_REFILL_MS | 630000 (10 min 30 s) | 630000 (10 min 30 s) |
| TIMER_FULL_MS | 1500000 (25 min) | 1500000 |
| MAX_CYCLE_MS | 2520000 (42 min) | 2520000 |
| Door-closed drop (lowmid) | 750000 | 750000 |
| WINDOW | 200 | 200 |
| Bands (Hz) | 60–600, 7875–9750, 9750–11625 | same |
| sendInterval | 300000 | 300000 |
| BLE payload | 2 bytes (running, empty) | 3 bytes (+ mic ok) |

- Old firmware: `old_washer_bluer.ino` variant, built Dec 18 2025. Read 2026-10-03 from full dump
  `dryer_bluer/sj-w10.bin` (sha256 `c16bd31bbcc6b816d65b52dc61c926da67b2034d2c0c4030288af6ef14099605`), `device_reads/sj-w10_old.json`.
  Values decoded from the code's constant pool; REFILL_HIGH / TIMER_REFILL_MS confirmed by disassembly.
- Sketch updated 2026-10-03 to match the old thresholds (generic sketch had REFILL_HIGH 32000 on either band and
  TIMER_REFILL_MS 600000). machineId string is WASHER_NODE_W10 (log only). Not reflashed yet.

## Pair 8 (sj-d8 / sj-w8, formerly a1-m6 / a1-m5)

### Dryer sj-d8 — ESP32 MAC b4:bf:e9:05:06:f8

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| machineId | a1-m6 | sj-d8 |
| washerMachineId | a1-m5 | sj-w8 |
| Washer BLE filter | WASHER_A3 | WASHER_W8 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | 0.65 | 0.65 |

- Old firmware: same `old_dryer_bluer.ino` build family as sj-d10 (built Dec 18 2025). Read 2026-10-03 with
  `tools/read_device_config.py` (no full dump; result in `device_reads/sj-d8_old.json`,
  segments sha256 `e0ef8c3b9da582e378dd5bcdcad3b37e7da01bdb0adbd20b4f21c8f723e29d30`).
- `dryer_bluer.ino` was set to these values 2026-10-03 for flashing; flash not yet confirmed.

### Washer sj-w8

Not read back. Operator 2026-10-03 (old module a1-m5): only `TIMER_REFILL_MS` differs, `630000`.

## Pair 7 (sj-d7 / sj-w7, formerly a1-m8 / a1-m7)

### Dryer sj-d7 — ESP32 MAC b4:bf:e9:05:04:58

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| machineId | a1-m8 | sj-d7 |
| washerMachineId | a1-m7 | sj-w7 |
| Washer BLE filter | WASHER_A4 | WASHER_W7 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | 0.65 | 0.65 |

- Old firmware: same `old_dryer_bluer.ino` build family as sj-d10 (built Dec 18 2025). Read 2026-10-03 with
  `tools/read_device_config.py` (no full dump; result in `device_reads/sj-d7_old.json`,
  segments sha256 `3951363c320dc77889047e6a1fbc42af19fe7260a8e4ccb3264dc7fa94ad4c10`).
- Flashed 2026-10-03 (per operator).

### Washer sj-w7 (formerly a1-m7) — ESP32 MAC b4:bf:e9:04:fc:80

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| BLE name | WASHER_A4 | WASHER_W7 |
| FIRST_FILL_HIGH2 (either band) | 68500 | 68500 |
| REFILL_HIGH | **32000, either band** (sj-w10: 51500, one band) | 32000, either band |
| TIMER_REFILL_MS | 630000 (10 min 30 s) | 630000 |
| Door-closed drop (lowmid) | **550000** (sj-w10: 750000) | 550000 |
| TIMER_FULL_MS / MAX_CYCLE_MS | 1500000 / 2520000 | same |
| WINDOW / bands / sendInterval | 200 / same as sj-w10 / 300000 | same |
| BLE payload | 2 bytes | 3 bytes (+ mic ok) |

- Old firmware: `old_washer_bluer.ino` variant, built Dec 18 2025. Read 2026-10-03 (partial read of strings +
  sketch constants, `device_reads/sj-w7_old.json`). REFILL_HIGH, its band count, TIMER_REFILL_MS and the door
  drop confirmed by disassembly of loop().
- Flashed 2026-10-03 (per operator).

## Pair 9 (sj-d9 / sj-w9, formerly a1-m4 / a1-m3)

### Dryer sj-d9 — ESP32 MAC ec:e3:34:46:06:34

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| machineId | a1-m4 | sj-d9 |
| washerMachineId | a1-m3 | sj-w9 |
| Washer BLE filter | WASHER_A2 | WASHER_W9 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | 0.65 | 0.65 |

- Old firmware: same `old_dryer_bluer.ino` build family as sj-d10 (built Dec 18 2025). Read 2026-10-03 with
  `tools/read_device_config.py` (`device_reads/sj-d9_old.json`,
  segments sha256 `971e9b4a52f2c3e955667891080ae200101752dac3697bc6c397712e7dbd7881`).
- `dryer_bluer.ino` set to these values 2026-10-03 for flashing; flash not yet confirmed.

### Washer sj-w9

Not read back. Operator 2026-10-03 (old module a1-m3): no values differ from the baseline sketch.

## Pair 6 (sj-d6 / sj-w6, formerly a1-m10 / a1-m9)

### Dryer sj-d6 — ESP32 MAC b4:bf:e9:05:cf:24

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| machineId | a1-m10 | sj-d6 |
| washerMachineId | a1-m9 | sj-w6 |
| Washer BLE filter | WASHER_A5 | WASHER_W6 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | 0.65 | 0.65 |

- Old firmware: same `old_dryer_bluer.ino` build family as sj-d10 (built Dec 18 2025). Read 2026-10-03 with
  `tools/read_device_config.py` (`device_reads/sj-d6_old.json`,
  segments sha256 `481e74a0f7f95c2b4651403d4ac6b5ab368af3830cb6f79ff7d4f929b9c31380`).
- `dryer_bluer.ino` set to these values 2026-10-03 for flashing; flash not yet confirmed.

### Washer sj-w6

Not read back. Operator 2026-10-03 (old module a1-m9): only `TIMER_REFILL_MS` differs, `630000`.

## Pair 5 (sj-d5 / sj-w5, formerly a1-m12 / a1-m11)

### Dryer sj-d5 — ESP32 MAC 70:4b:ca:99:5e:a0

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| machineId | a1-m12 | sj-d5 |
| washerMachineId | a1-m11 | sj-w5 |
| Washer BLE filter | WASHER_A6 | WASHER_W5 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | 0.65 | 0.65 |

- Old firmware: same `old_dryer_bluer.ino` build family as sj-d10 (built Dec 18 2025). Read 2026-10-03 with
  `tools/read_device_config.py` (`device_reads/sj-d5_old.json`,
  segments sha256 `43355622e7e6c2c572f00f452d5a0ee5e5e0eb9f5d1331e06910a9296d0ccdf9`).
- `dryer_bluer.ino` set to these values 2026-10-03 for flashing; flash not yet confirmed.

### Washer sj-w5 (formerly a1-m11) — ESP32 MAC b4:bf:e9:05:04:14

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| BLE name | WASHER_A6 | WASHER_W5 |
| FIRST_FILL_HIGH2 (either band) | 68500 | 68500 |
| REFILL_HIGH | 32000, either band | 32000, either band |
| TIMER_REFILL_MS | 630000 (10 min 30 s) | 630000 |
| Door-closed drop (lowmid) | 750000 | 750000 |
| TIMER_FULL_MS / MAX_CYCLE_MS | 1500000 / 2520000 | same |
| WINDOW / bands / sendInterval | 200 / same as sj-w10 / 300000 | same |
| BLE payload | 2 bytes | 3 bytes (+ mic ok) |

- Old firmware: `old_washer_bluer.ino` variant, built Dec 18 2025. Read 2026-10-03 (partial read of strings +
  sketch constants, `device_reads/sj-w5_old.json`). REFILL_HIGH (2 bands), TIMER_REFILL_MS, door drop and
  timers confirmed by disassembly of loop().
- Flashed 2026-10-03 (per operator).

## Pair 4 (sj-d4 / sj-w4, formerly a1-m14 / a1-m13)

### Dryer sj-d4 — ESP32 MAC b4:bf:e9:04:f0:e4

| Setting | Old firmware (read back) | New values (sketch) |
|---|---|---|
| machineId | a1-m14 | sj-d4 |
| washerMachineId | a1-m13 | sj-w4 |
| Washer BLE filter | WASHER_A7 | WASHER_W4 |
| DYRERONTHRESHHOLD | 2.45 | 2.45 |
| DOORCLOSINGCHANGE | **0.55** | 0.55 |

- Old firmware: same `old_dryer_bluer.ino` build family as sj-d10 (built Dec 18 2025). Read 2026-10-03 with
  `tools/read_device_config.py` (`device_reads/sj-d4_old.json`,
  segments sha256 `bf7751083951a179332fe6e440a99133cf094041403bb0a2eaf1466ca5dd2453`).
- `dryer_bluer.ino` set to these values 2026-10-03 for flashing; flash not yet confirmed.

### Washer sj-w4

Not read back. Operator 2026-10-03 (old module a1-m13): only `TIMER_REFILL_MS` differs, `630000`.

## Pair 3 (sj-d3 / sj-w3, formerly a1-m16 / a1-m15)

### Dryer sj-d3

Not read back. Operator 2026-10-03: baseline `2.45` / `0.65`.

### Washer sj-w3 — ESP32 MAC 70:4b:ca:9b:70:70

| Setting | Old firmware (read back) |
|---|---|
| BLE name | WASHER_A8 |
| TIMER_REFILL_MS | **630000** (10 min 30 s) |
| FIRST_FILL_HIGH2 | 68500 |
| REFILL_HIGH | 32000 |
| Door-closed drop (lowmid) | 750000 |
| TIMER_FULL_MS / MAX_CYCLE_MS / sendInterval | 1500000 / 2520000 / 300000 |

- Read 2026-10-03 from a 2 MB dump, values decoded from the constant pool next to the 7875/9750/11625 band
  edges. Number of bands REFILL_HIGH checks was not confirmed by disassembly.

## Pair 2 (sj-d2 / sj-w2, formerly a1-m18 / a1-m17)

### Dryer sj-d2

Not read back. Operator 2026-10-03: baseline `2.45` / `0.65`.

### Washer sj-w2

| Setting | Old firmware (read back) |
|---|---|
| BLE name | WASHER_A9 |
| TIMER_REFILL_MS | 600000 (10 min) |
| FIRST_FILL_HIGH2 | 68500 |
| REFILL_HIGH | 32000 |
| Door-closed drop (lowmid) | 750000 |
| TIMER_FULL_MS / MAX_CYCLE_MS / sendInterval | 1500000 / 2520000 / 300000 |

- Read 2026-10-03 from a full 4 MB dump, same method as sj-w3. Band count for REFILL_HIGH not confirmed by
  disassembly.

## Pair 1 (sj-d1 / sj-w1, formerly a1-m20 / a1-m19)

The firmware-trial pair: flashed with the hardened sketch 2026-09-22, so their old values were not read back.
Operator 2026-10-03: both match the baseline sketch (dryer `2.45` / `0.65`; washer baseline).

## Reading a board's config fast

Instead of dumping all 4 MB (~6 min at 115200 baud), read only the partition table and the
segments that hold the IDs/thresholds (read-only, then resets the board). ~20 s at the default
230400 baud (460800+ corrupts reads on our adapters). Dryer firmware only (washers: see how sj-w7 was read).

    /opt/homebrew/opt/esptool/libexec/bin/python tools/read_device_config.py --port /dev/cu.usbserial-0001

Also works on an existing dump: `python3 tools/read_device_config.py --file <dump>.bin`.
For a full dump, `-b 230400` is the fastest rate that reads reliably here.
Flash dumps contain the eduroam password in plaintext: never commit `*.bin`.
