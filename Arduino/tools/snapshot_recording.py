#!/usr/bin/env python3
"""Turns a recording CSV from /test/live into snapshot graphs of only the moments that matter.

On /test/live choose the pair, press Start recording, run your tests, then Download CSV
(a recording lasts a day at most, and the website deletes it 3 days after it ends). Then:

    pip3 install matplotlib
    python3 snapshot_recording.py sj-4_2026-11-04-14-03.csv
    python3 snapshot_recording.py day1.csv day2.csv --out my_test

Quiet stretches are skipped: washer bands both under 10,000, washer door drop under 700,000,
dryer avg10 under 2.2 and dryer door jump under 2. Everything else becomes a PNG:
  washer  ~20 s around every burst of water (first fill, refills, a neighbour) or door drop,
          and ~20 s around the moment it stops running, so you know when it ended
  dryer   the first 2 min of each cycle and its end; the middle only where avg10 dips out of it.
          Door jumps count only outside a cycle, since a running drum shakes the door too.
Times are in this computer's local time, to match your notes. Reads files only: it never
talks to a node or the website.
"""
import argparse
import csv
import sys
from collections import namedtuple
from datetime import datetime, timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # files only, no window
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

WASHER_WATER = 10_000
WASHER_DOOR = 700_000
DRYER_ON = 2.2
DRYER_DOOR = 2.0

PERIOD = timedelta(seconds=5)  # each reading is the highest value of the 5 s ending at its time
MIN_SNAPSHOT = timedelta(seconds=20)
WASHER_PAD = timedelta(seconds=10)
WASHER_GAP = timedelta(seconds=15)  # bursts closer than this are one snapshot
CYCLE_GAP = timedelta(seconds=60)  # avg10 dips shorter than this stay inside the cycle
CYCLE_BEFORE = timedelta(seconds=30)
CYCLE_START = timedelta(minutes=2)
CYCLE_END = timedelta(minutes=2)
CYCLE_AFTER = timedelta(seconds=60)
DIP_PAD = timedelta(seconds=30)
DOOR_PAD = timedelta(seconds=20)

Dryer = namedtuple("Dryer", "t avg10 door running empty")
Washer = namedtuple("Washer", "t w1 w2 drop running empty")
Window = namedtuple("Window", "start end label")


def load(paths):
    dryer, washer = {}, {}
    for path in paths:
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                t = datetime.fromisoformat(row["time"].replace("Z", "+00:00")).astimezone()
                if row["machine"] == "dryer":
                    dryer[t] = Dryer(t, float(row["avg10"]), float(row["door_jump"]),
                                     int(row["running"]), int(row["empty"]))
                elif row["machine"] == "washer":
                    washer[t] = Washer(t, float(row["water1"]), float(row["water2"]), float(row["door_drop"]),
                                       int(row["running"]), int(row["empty"]))
    # Keyed by time, so parts that overlap at their boundary don't double a reading.
    return [dryer[t] for t in sorted(dryer)], [washer[t] for t in sorted(washer)]


def group(times, gap):
    """Consecutive times no more than gap apart, as [first, last] pairs."""
    groups = []
    for t in times:
        if groups and t - groups[-1][1] <= gap:
            groups[-1][1] = t
        else:
            groups.append([t, t])
    return groups


def widen(start, end):
    short = MIN_SNAPSHOT - (end - start)
    return (start - short / 2, end + short / 2) if short > timedelta(0) else (start, end)


def merge(windows):
    merged = []
    for w in sorted(windows):
        if merged and w.start <= merged[-1].end:
            last = merged.pop()
            labels = last.label.split(" + ")
            label = last.label if w.label in labels else f"{last.label} + {w.label}"
            merged.append(Window(last.start, max(last.end, w.end), label))
        else:
            merged.append(w)
    return merged


def rows_in(rows, w):
    return [r for r in rows if w.start <= r.t <= w.end + PERIOD]


def washer_label(rows):
    labels = []
    if any(r.drop >= WASHER_DOOR for r in rows):
        labels.append("door")
    wet = [i for i, r in enumerate(rows) if max(r.w1, r.w2) >= WASHER_WATER]
    if wet:
        before = rows[wet[0] - 1].running if wet[0] > 0 else rows[0].running
        if not before and any(r.running for r in rows[wet[0]:]):
            labels.append("first fill")
        elif before:
            labels.append("water while running")
        else:
            labels.append("water while idle (neighbour?)")
    if any(a.running and not b.running for a, b in zip(rows, rows[1:])):
        labels.append("washer ended")
    return " + ".join(labels)


def washer_windows(washer):
    loud = [r.t for r in washer if max(r.w1, r.w2) >= WASHER_WATER or r.drop >= WASHER_DOOR]
    windows = []
    for first, last in group(loud, WASHER_GAP):
        start, end = widen(first - PERIOD - WASHER_PAD, last + WASHER_PAD)
        windows.append(Window(start, end, ""))
    # Running going false: quiet on the graph, but it is when the washer says the cycle ended.
    for prev, r in zip(washer, washer[1:]):
        if prev.running and not r.running:
            windows.append(Window(prev.t - PERIOD - WASHER_PAD, r.t + WASHER_PAD, ""))
    return [Window(w.start, w.end, washer_label(rows_in(washer, w))) for w in merge(windows)]


def dryer_windows(dryer):
    windows, busy = [], []
    for first, last in group([r.t for r in dryer if r.avg10 >= DRYER_ON], CYCLE_GAP):
        start = first - PERIOD
        busy.append((start - CYCLE_BEFORE, last + CYCLE_AFTER))
        if last - start <= CYCLE_START + CYCLE_END:
            windows.append(Window(start - CYCLE_BEFORE, last + CYCLE_AFTER, "cycle"))
            continue
        windows.append(Window(start - CYCLE_BEFORE, start + CYCLE_START, "cycle start"))
        windows.append(Window(last - CYCLE_END, last + CYCLE_AFTER, "cycle end"))
        middle = [r.t for r in dryer if start + CYCLE_START < r.t < last - CYCLE_END and r.avg10 < DRYER_ON]
        for d_first, d_last in group(middle, PERIOD * 2):
            windows.append(Window(d_first - PERIOD - DIP_PAD, d_last + DIP_PAD, "mid-cycle dip"))

    # Inside a cycle or its snapshots the door jump is the drum shaking, or already on screen.
    doors = [r.t for r in dryer if r.door >= DRYER_DOOR and not any(s <= r.t <= e for s, e in busy)]
    for first, last in group(doors, PERIOD * 2):
        start, end = widen(first - PERIOD - DOOR_PAD, last + DOOR_PAD)
        windows.append(Window(start, end, "door"))
    return merge(windows)


def shade_running(ax, rows):
    for r in rows:
        if r.running:
            ax.axvspan(r.t - PERIOD, r.t, color="#fde68a", alpha=0.5, linewidth=0)


def finish(ax, ylabel):
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper left", fontsize="small")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))


def plot_dryer(rows, w, path):
    fig, ax = plt.subplots(figsize=(10, 4))
    shade_running(ax, rows)
    times = [r.t for r in rows]
    ax.plot(times, [r.avg10 for r in rows], "o-", color="#2563eb", markersize=3, label="avg10 (max per 5 s)")
    ax.plot(times, [r.door for r in rows], "o-", color="#7c3aed", markersize=3, label="door jump (max per 5 s)")
    ax.axhline(DRYER_ON, color="#2563eb", linestyle="--", linewidth=1, label=f"avg10 filter {DRYER_ON}")
    ax.axhline(DRYER_DOOR, color="#7c3aed", linestyle="--", linewidth=1, label=f"door filter {DRYER_DOOR}")
    ax.set_xlim(w.start, w.end + PERIOD)
    finish(ax, "dryer")
    fig.suptitle(f"Dryer: {w.label}   {w.start:%Y-%m-%d %H:%M:%S} to {w.end:%H:%M:%S}   (yellow = running)")
    fig.autofmt_xdate()
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


def plot_washer(rows, w, path):
    fig, (water, door) = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
    times = [r.t for r in rows]
    shade_running(water, rows)
    shade_running(door, rows)
    water.plot(times, [r.w1 for r in rows], "o-", color="#2563eb", markersize=3, label="water 7.9-9.8 kHz")
    water.plot(times, [r.w2 for r in rows], "o-", color="#0d9488", markersize=3, label="water 9.8-11.6 kHz")
    water.axhline(WASHER_WATER, color="#dc2626", linestyle="--", linewidth=1, label=f"filter {WASHER_WATER:,}")
    door.plot(times, [r.drop for r in rows], "o-", color="#7c3aed", markersize=3, label="door drop (max per 5 s)")
    door.axhline(WASHER_DOOR, color="#dc2626", linestyle="--", linewidth=1, label=f"filter {WASHER_DOOR:,}")
    door.set_xlim(w.start, w.end + PERIOD)
    finish(water, "water bands")
    finish(door, "door band drop")
    fig.suptitle(f"Washer: {w.label}   {w.start:%Y-%m-%d %H:%M:%S} to {w.end:%H:%M:%S}   (yellow = running)")
    fig.autofmt_xdate()
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="Snapshot graphs from a /test/live recording CSV.")
    parser.add_argument("csv", nargs="+", help="recording CSV file(s), all parts of one recording")
    parser.add_argument("--out", help="folder for the PNGs (default: next to the first CSV)")
    args = parser.parse_args()

    dryer, washer = load(args.csv)
    if not dryer and not washer:
        sys.exit("No readings in those files.")
    first = Path(args.csv[0])
    out = Path(args.out) if args.out else first.with_name(f"{first.stem}_snapshots")
    out.mkdir(parents=True, exist_ok=True)

    shots = [("dryer", w) for w in dryer_windows(dryer)] + [("washer", w) for w in washer_windows(washer)]
    shots.sort(key=lambda s: s[1].start)

    with open(out / "index.csv", "w", newline="", encoding="utf-8") as f:
        index = csv.writer(f)
        index.writerow(["file", "machine", "what", "start", "end", "peak avg10", "peak dryer door jump",
                        "peak water 1", "peak water 2", "peak washer door drop"])
        for n, (machine, w) in enumerate(shots, 1):
            slug = w.label.replace(" + ", "_").replace(" ", "-").replace("(", "").replace(")", "").replace("?", "")
            name = f"{n:03d}_{machine}_{slug}_{w.start:%Y-%m-%d_%H-%M-%S}.png"
            if machine == "dryer":
                rows = rows_in(dryer, w)
                plot_dryer(rows, w, out / name)
                peaks = [max(r.avg10 for r in rows), max(r.door for r in rows), "", "", ""]
            else:
                rows = rows_in(washer, w)
                plot_washer(rows, w, out / name)
                peaks = ["", "", max(r.w1 for r in rows), max(r.w2 for r in rows), max(r.drop for r in rows)]
            index.writerow([name, machine, w.label, f"{w.start:%Y-%m-%d %H:%M:%S}", f"{w.end:%H:%M:%S}", *peaks])
            print(f"{w.start:%Y-%m-%d %H:%M:%S}  {machine:6}  {w.label}")

    span = f"{min(r.t for r in dryer + washer):%Y-%m-%d %H:%M} to {max(r.t for r in dryer + washer):%Y-%m-%d %H:%M}"
    print(f"\n{len(dryer)} dryer and {len(washer)} washer readings, {span}.")
    print(f"{len(shots)} snapshots in {out} (list in index.csv).")


if __name__ == "__main__":
    main()
