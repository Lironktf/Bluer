#!/usr/bin/env python3
"""Live plot of the combined dryer + washer node, one graph per sensor.

Reads the plot line printed by dryer_and_washer_bluer.ino over the USB-C port.
Close the Arduino Serial Monitor/Plotter first: only one program can hold the port.

    pip3 install pyserial matplotlib
    python3 plot_dryer_washer.py            # finds the ESP32 automatically
    python3 plot_dryer_washer.py /dev/cu.usbmodem1101
"""
import glob
import sys
import threading
import time
from collections import deque

import matplotlib.pyplot as plt
import serial
from serial.tools import list_ports
from matplotlib.animation import FuncAnimation

HISTORY = 1200  # samples kept on screen: 60 s at one line per 50 ms
WASHER_YMAX = 80000  # values above this are drawn flat at the top and flagged in the legend
ESPRESSIF_VID = 0x303A  # the ESP32-S3's own USB port

DRYER_KEYS = ["DryerAvg10", "DryerActivitySmall", "DryerOnLine"]
WASHER_KEYS = ["WasherWater_7875_9750", "WasherWater_9750_11625", "WasherDoor",
               "FirstFillLine", "RefillLine"]
# Undo the sketch's plot scaling so the washer graph shows the firmware's real numbers.
UNSCALE = {"WasherWater_7875_9750": 10000, "WasherWater_9750_11625": 10000,
           "FirstFillLine": 10000, "RefillLine": 10000, "WasherDoor": 1000000}

series = {k: deque(maxlen=HISTORY) for k in DRYER_KEYS + WASHER_KEYS}
lock = threading.Lock()
stats = {"plot": 0, "other": 0, "status": "connecting"}


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid == ESPRESSIF_VID:
            return p.device
    usb = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
    if usb:
        return usb[0]
    print("No ESP32 found. Ports seen:")
    for p in ports:
        print(f"  {p.device}  {p.description}")
    sys.exit("Plug the board in, or pass the port, e.g. /dev/cu.usbmodem1101")


def parse(line):
    values = {}
    for part in line.split(","):
        key, sep, value = part.partition(":")
        if not sep:
            continue
        try:
            values[key.strip()] = float(value)
        except ValueError:
            continue
    return values if "DryerAvg10" in values else None


def reader(port):
    while True:
        try:
            # exclusive: fail loudly instead of silently sharing the port with the Arduino IDE
            with serial.Serial(port, 115200, timeout=1, exclusive=True) as ser:
                stats["status"] = "connected"
                print(f"Connected to {port}. If the board just reset, data starts after up to ~15 s.")
                while True:
                    line = ser.readline().decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    values = parse(line)
                    if values is None:
                        stats["other"] += 1
                        print(line)  # status and WiFi messages go to the terminal
                        continue
                    stats["plot"] += 1
                    with lock:
                        for key in series:
                            series[key].append(values.get(key, float("nan")) * UNSCALE.get(key, 1))
        except (serial.SerialException, OSError) as e:
            stats["status"] = "disconnected"
            print(f"Port problem: {e}")
            print("Close the Arduino Serial Monitor/Plotter if open. Retrying in 2 s...")
            time.sleep(2)


def main():
    port = find_port()
    print(f"Reading {port} (Ctrl+C to stop)")
    threading.Thread(target=reader, args=(port,), daemon=True).start()

    fig, (ax_dryer, ax_washer) = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
    fig.canvas.manager.set_window_title("Bluer dryer + washer")
    lines = {}
    for ax, keys, title in ((ax_dryer, DRYER_KEYS, "Dryer (MPU6050)"),
                            (ax_washer, WASHER_KEYS, "Washer (INMP441), real units")):
        for key in keys:
            style = "--" if key.endswith("Line") else "-"
            (lines[key],) = ax.plot([], [], style, label=key)
        ax.set_title(title)
        ax.legend(loc="upper left", fontsize="small")
        ax.grid(True, alpha=0.3)
    ax_washer.set_xlabel("samples (50 ms each)")

    def update(_):
        with lock:
            snapshot = {k: list(v) for k, v in series.items()}
        for key in DRYER_KEYS:
            lines[key].set_data(range(len(snapshot[key])), snapshot[key])
        for key in WASHER_KEYS:
            data = snapshot[key]
            lines[key].set_data(range(len(data)), [min(v, WASHER_YMAX) for v in data])
            latest = data[-1] if data else float("nan")
            lines[key].set_label(f"{key}  ABOVE {WASHER_YMAX:,} (now {latest:,.0f})"
                                 if latest > WASHER_YMAX else key)
        ax_washer.legend(loc="upper left", fontsize="small")

        ax_dryer.set_xlim(0, HISTORY)
        # Fixed 0-10 so small wiggles don't fill the screen; grows only if a value goes above 10.
        peak = max((v for k in DRYER_KEYS for v in snapshot[k] if v == v), default=0)
        ax_dryer.set_ylim(0, max(10, peak * 1.1))
        ax_washer.set_xlim(0, HISTORY)
        ax_washer.set_ylim(0, WASHER_YMAX * 1.02)  # small headroom so a capped line stays visible
        fig.suptitle(f"{port}: {stats['status']}, plot lines received: {stats['plot']}, "
                     f"other lines: {stats['other']}", fontsize="small")
        return list(lines.values())

    _anim = FuncAnimation(fig, update, interval=200, cache_frame_data=False)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
