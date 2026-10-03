#!/usr/bin/env python3
"""Read the machine IDs, washer BLE name and detection thresholds out of a
deployed dryer ESP32 without dumping the whole 4 MB flash.

A full `esptool read-flash 0 0x400000` takes ~6 minutes at 115200 baud. This
reads only what is needed (partition table, app header, the read-only data
segment that holds the strings, the .data segment that holds the initialised
globals, and the first 64 KB of code for cross-checking) at 230400 baud, which
is a few hundred KB and ~20 seconds.

It is read-only: it only issues flash *reads*, then hard-resets the board so
it goes back to running its firmware.

Usage
  Live board (needs esptool's Python, which Homebrew installs separately):
    /opt/homebrew/opt/esptool/libexec/bin/python tools/read_device_config.py \
        --port /dev/cu.usbserial-0001
  Existing full dump (any python3):
    python3 tools/read_device_config.py --file dryer_bluer/a1-m2.bin

Add --json out.json to also save the result. Credentials compiled into the
firmware are never printed or saved; only IDs, URL, BLE name and floats are.
"""

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from datetime import datetime, timezone

PART_TABLE_ADDR = 0x8000
PART_TABLE_LEN = 0xC00
IROM_CHECK_LEN = 0x10000

# ESP32 virtual address windows (ESP32 classic, which is what these nodes are).
DROM = (0x3F400000, 0x3F800000)  # read-only data: string literals
DRAM = (0x3FFAE000, 0x40000000)  # .data: initialised globals
IROM = (0x400C2000, 0x40C00000)  # flash-mapped code

ID_RE = re.compile(r"^[a-z]+[0-9]*-[a-z]+[0-9]+$")


def in_range(addr, rng):
    return rng[0] <= addr < rng[1]


# ---------------------------------------------------------------- flash access

class FileFlash:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        self.mac = None

    def read(self, addr, size):
        chunk = self.data[addr:addr + size]
        if len(chunk) != size:
            raise ValueError(f"dump is too short to read {size:#x} bytes at {addr:#x}")
        return chunk

    def close(self):
        pass


class LiveFlash:
    def __init__(self, port, baud):
        try:
            from esptool.cmds import attach_flash, detect_chip, run_stub
        except ImportError:
            sys.exit("esptool is not importable from this python. Run with "
                     "/opt/homebrew/opt/esptool/libexec/bin/python instead.")
        esp = detect_chip(port, baud=115200)
        if esp.CHIP_NAME != "ESP32":
            sys.exit(f"Expected an ESP32, found {esp.CHIP_NAME}; address windows would be wrong.")
        esp = run_stub(esp)
        if baud != 115200:
            esp.change_baud(baud)
        attach_flash(esp)
        self.esp = esp
        self.mac = ":".join(f"{b:02x}" for b in esp.read_mac())

    def read(self, addr, size):
        # The stub streams whole 4 KB blocks, so read an aligned superset and slice.
        start = addr & ~0xFFF
        end = (addr + size + 0xFFF) & ~0xFFF
        for attempt in range(3):
            try:
                data = self.esp.read_flash(start, end - start, None)
                break
            except Exception as e:  # esptool raises FatalError on a corrupt block
                if attempt == 2:
                    raise
                print(f"read at {start:#x} failed ({e}), retrying")
                self.esp.flush_input()
        chunk = data[addr - start:addr - start + size]
        if len(chunk) != size:
            raise IOError(f"short read at {addr:#x}: got {len(chunk)} of {size} bytes")
        return chunk

    def close(self):
        from esptool.cmds import reset_chip
        reset_chip(self.esp, "hard-reset")
        self.esp._port.close()


# ---------------------------------------------------------------- image parsing

def parse_partitions(raw):
    parts = []
    for i in range(0, len(raw), 32):
        e = raw[i:i + 32]
        if e[:2] != b"\xaa\x50":
            break
        ptype, sub = e[2], e[3]
        off, size = struct.unpack("<II", e[4:12])
        name = e[12:28].split(b"\0")[0].decode("ascii", "replace")
        parts.append({"type": ptype, "subtype": sub, "offset": off, "size": size, "name": name})
    if not parts:
        raise ValueError("no partition table at 0x8000")
    return parts


def pick_app(parts, flash):
    """Return (partition, reason). Mirrors the bootloader: valid otadata -> that
    OTA slot, otherwise factory, otherwise ota_0."""
    apps = [p for p in parts if p["type"] == 0]
    otas = sorted((p for p in apps if 0x10 <= p["subtype"] < 0x20), key=lambda p: p["subtype"])
    factory = next((p for p in apps if p["subtype"] == 0x00), None)
    otadata = next((p for p in parts if p["type"] == 1 and p["subtype"] == 0x00), None)

    if otas and otadata:
        seqs = []
        for sector in (0, 0x1000):
            seq = struct.unpack("<I", flash.read(otadata["offset"] + sector, 4))[0]
            if seq not in (0, 0xFFFFFFFF):
                seqs.append(seq)
        if seqs:
            slot = (max(seqs) - 1) % len(otas)
            return otas[slot], f"otadata seq={max(seqs)} -> {otas[slot]['name']} (CRC not checked)"
    if factory:
        return factory, "factory partition (no valid otadata)"
    if otas:
        return otas[0], "first OTA slot (no valid otadata, no factory)"
    raise ValueError("no app partition")


def parse_segments(flash, app_off):
    hdr = flash.read(app_off, 24)
    if hdr[0] != 0xE9:
        raise ValueError(f"no app image at {app_off:#x} (magic {hdr[0]:#x})")
    nseg = hdr[1]
    segs, pos = [], app_off + 24
    for _ in range(nseg):
        load, length = struct.unpack("<II", flash.read(pos, 8))
        segs.append({"load": load, "len": length, "file": pos + 8})
        pos += 8 + length
    return segs


class Image:
    def __init__(self, flash, segs):
        self.drom = next(s for s in segs if in_range(s["load"], DROM))
        self.dram = next(s for s in segs if in_range(s["load"], DRAM))
        irom = next(s for s in segs if in_range(s["load"], IROM))
        self.drom_bytes = flash.read(self.drom["file"], self.drom["len"])
        self.dram_bytes = flash.read(self.dram["file"], self.dram["len"])
        n = min(irom["len"], IROM_CHECK_LEN)
        self.irom = irom
        self.irom_bytes = flash.read(irom["file"], n)

    def cstr(self, vaddr):
        o = vaddr - self.drom["load"]
        if not 0 <= o < len(self.drom_bytes):
            return None
        end = self.drom_bytes.find(b"\0", o)
        if end < 0 or end - o > 512:
            return None
        try:
            return self.drom_bytes[o:end].decode("utf-8")
        except UnicodeDecodeError:
            return None

    def find_str(self, needle):
        """Virtual address of the NUL-terminated string containing `needle`."""
        i = self.drom_bytes.find(needle.encode())
        if i < 0:
            return None
        start = self.drom_bytes.rfind(b"\0", 0, i) + 1
        return self.drom["load"] + start

    def data_word(self, vaddr):
        o = vaddr - self.dram["load"]
        if not 0 <= o <= len(self.dram_bytes) - 4:
            return None
        return self.dram_bytes[o:o + 4]

    def app_desc(self):
        d = self.drom_bytes[:256]
        if struct.unpack("<I", d[:4])[0] != 0xABCD5432:
            return {}
        f = lambda a, b: d[a:b].split(b"\0")[0].decode("ascii", "replace")
        return {"version": f(16, 48), "project": f(48, 80), "time": f(80, 96),
                "date": f(96, 112), "idf": f(112, 144)}


# ---------------------------------------------------------------- analysis

def human_float(word):
    """A float a person typed (at most 3 decimals), else None. Pointers into
    IRAM/flash also decode as floats near 2.1 but almost never land on 3 decimals."""
    v = struct.unpack("<f", word)[0]
    if not math.isfinite(v) or not 0.001 <= abs(v) <= 1000:
        return None
    r = round(v, 3)
    return r if struct.pack("<f", r) == word else None


def literal_before(img, string_vaddr):
    """In Xtensa literal pools the sketch's l32r constants sit next to each other.
    Return the literal that immediately precedes the pointer to `string_vaddr`."""
    if string_vaddr is None:
        return None
    pat = struct.pack("<I", string_vaddr)
    b = img.irom_bytes
    i = b.find(pat)
    while i != -1 and i % 4:
        i = b.find(pat, i + 1)
    if i < 4:
        return None
    return struct.unpack("<I", b[i - 4:i])[0]


def analyse(img):
    out = {"warnings": []}
    dram, base = img.dram_bytes, img.dram["load"]

    id_vars, url_vars, float_vars = {}, {}, {}
    for i in range(0, len(dram) - 3, 4):
        w = dram[i:i + 4]
        u = struct.unpack("<I", w)[0]
        if in_range(u, DROM):
            s = img.cstr(u)
            if s and ID_RE.match(s):
                id_vars[base + i] = s
            elif s and s.startswith("http"):
                url_vars[base + i] = s
            continue
        f = human_float(w)
        if f is not None:
            float_vars[base + i] = f

    out["id_strings_in_data"] = sorted(set(id_vars.values()))
    out["server_url"] = sorted(set(url_vars.values()))
    out["ble_names"] = sorted({m.decode() for m in re.findall(rb"(?<=\0)WASHER_[A-Za-z0-9_]+(?=\0)", img.drom_bytes)})
    out["float_globals"] = {hex(a): v for a, v in float_vars.items()}

    # machineId: the pointer variable printf'd with "Machine ID: %s".
    mid_var = literal_before(img, img.find_str("Machine ID: %s"))
    machine_id = id_vars.get(mid_var)
    out["machine_id"] = machine_id
    out["machine_id_method"] = "literal pool next to 'Machine ID' print" if machine_id else None

    others = [s for a, s in id_vars.items() if s != machine_id]
    if machine_id and len(others) == 1:
        out["washer_machine_id"] = others[0]
    else:
        out["washer_machine_id"] = None
        out["warnings"].append(f"could not single out washer id from {sorted(id_vars.values())}")

    # DOORCLOSINGCHANGE: the float loaded just before the "Door event" print.
    door_var = literal_before(img, img.find_str("Door event detected"))
    door = float_vars.get(door_var)
    out["DOORCLOSINGCHANGE"] = door

    # DYRERONTHRESHHOLD: declared directly next to DOORCLOSINGCHANGE in every
    # dryer sketch, so it is the float in the neighbouring .data word.
    neighbours = []
    if door is not None:
        for a in (door_var - 4, door_var + 4):
            w = img.data_word(a)
            f = human_float(w) if w else None
            if f is not None:
                neighbours.append(f)
    on = neighbours[0] if len(neighbours) == 1 else None
    out["DYRERONTHRESHHOLD"] = on
    if door is None or on is None:
        out["warnings"].append("threshold cross-check failed; verify by disassembling loop()")
    if len(out["ble_names"]) != 1:
        out["warnings"].append(f"expected exactly 1 WASHER_* BLE name, found {out['ble_names']}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port of a live board, e.g. /dev/cu.usbserial-0001")
    src.add_argument("--file", help="full flash dump made with esptool read-flash 0 0x400000")
    ap.add_argument("--baud", type=int, default=230400)  # 460800+ corrupts reads on our USB-serial adapters
    ap.add_argument("--json", help="also write the result to this JSON file")
    args = ap.parse_args()

    flash = LiveFlash(args.port, args.baud) if args.port else FileFlash(args.file)
    started = datetime.now(timezone.utc)
    try:
        parts = parse_partitions(flash.read(PART_TABLE_ADDR, PART_TABLE_LEN))
        app, why = pick_app(parts, flash)
        segs = parse_segments(flash, app["offset"])
        img = Image(flash, segs)
    finally:
        flash.close()

    result = {
        "read_at_utc": started.isoformat(timespec="seconds"),
        "source": args.port or args.file,
        "mac": flash.mac,
        "app_partition": f"{app['name']} @ {app['offset']:#x} ({why})",
        "firmware": img.app_desc(),
        # Hash of exactly the bytes analysed, to tell two builds apart.
        "segments_sha256": hashlib.sha256(img.drom_bytes + img.dram_bytes + img.irom_bytes).hexdigest(),
        "seconds": round((datetime.now(timezone.utc) - started).total_seconds(), 1),
    }
    result.update(analyse(img))

    for k in ("source", "mac", "app_partition", "firmware", "seconds", "machine_id", "washer_machine_id",
              "ble_names", "DYRERONTHRESHHOLD", "DOORCLOSINGCHANGE", "server_url", "id_strings_in_data",
              "float_globals", "segments_sha256"):
        print(f"{k:20} {result[k]}")
    for w in result["warnings"]:
        print(f"WARNING: {w}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(result, f, indent=2)
    return 1 if result["warnings"] else 0


if __name__ == "__main__":
    sys.exit(main())
