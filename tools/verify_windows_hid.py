"""Exercise the live USB gamepad through Windows HID and WinMM.

Requires ``pip install hidapi``. Keep both the COM and USB/OTG cables connected.
"""

import argparse
import ctypes
import json
import time
import urllib.request
from ctypes import wintypes

import hid


VID = 0x0F0D
URL = "http://192.168.1.27:7777"
CONTROLS = {
    "cross": (0x20, None),
    "circle": (0x40, None),
    "square": (0x10, None),
    "triangle": (0x80, None),
    "up": (None, 0),
    "right": (None, 2),
    "down": (None, 4),
    "left": (None, 6),
}


class JOYINFOEX(ctypes.Structure):
    _fields_ = [
        (name, wintypes.DWORD)
        for name in (
            "dwSize", "dwFlags", "dwXpos", "dwYpos", "dwZpos", "dwRpos",
            "dwUpos", "dwVpos", "dwButtons", "dwButtonNumber", "dwPOV",
            "dwReserved1", "dwReserved2",
        )
    ]


winmm = ctypes.WinDLL("winmm")
winmm.joyGetNumDevs.restype = wintypes.UINT
winmm.joyGetPosEx.argtypes = [wintypes.UINT, ctypes.POINTER(JOYINFOEX)]
winmm.joyGetPosEx.restype = wintypes.UINT


def joystick_id():
    for index in range(winmm.joyGetNumDevs()):
        if joystick_state(index) is not None:
            return index
    raise RuntimeError("Windows has no connected game controller")


def joystick_state(index):
    state = JOYINFOEX()
    state.dwSize = ctypes.sizeof(state)
    state.dwFlags = 0xFF
    if winmm.joyGetPosEx(index, ctypes.byref(state)) != 0:
        return None
    return state.dwButtons, state.dwPOV


def post(endpoint, payload):
    body = json.dumps(payload).encode()
    request = urllib.request.Request(
        URL + endpoint, body, {"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        response.read()


def sample(device, index, seconds, face_mask, hat):
    total = hits = windows_hits = 0
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        report = device.read(64)
        if report:
            if len(report) != 64 or report[0] != 1:
                raise AssertionError(f"Unexpected HID report: {len(report)} bytes, ID {report[0]:02x}")
            total += 1
            if (report[5] & 0xF0 == face_mask if face_mask is not None
                    else report[5] & 0x0F == hat):
                hits += 1
        state = joystick_state(index)
        if state and (state[0] != 0 if face_mask is not None else state[1] != 65535):
            windows_hits += 1
        time.sleep(0.002)
    return total, hits, windows_hits


def main():
    global URL
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default=URL)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=0x0163)
    args = parser.parse_args()
    URL = args.url.rstrip("/")
    path = next(
        device["path"] for device in hid.enumerate(VID, args.pid)
        if device["usage_page"] == 1 and device["usage"] == 5
    )
    device = hid.device()
    device.open_path(path)
    device.set_nonblocking(True)
    index = joystick_id()
    print(f"Windows joystick ID {index}")
    try:
        for name, (face_mask, hat) in CONTROLS.items():
            post("/api/release", {})
            time.sleep(0.08)
            while device.read(64):
                pass
            post("/api/press", {"button": name, "hold": True})
            try:
                # Discard reports queued before the HTTP press reached the device.
                time.sleep(0.12)
                while device.read(64):
                    pass
                total, hits, windows_hits = sample(device, index, 0.4, face_mask, hat)
            finally:
                post("/api/release", {})
            print(f"{name}: HID {hits}/{total} pressed, Windows {windows_hits} active samples")
            if total < 20 or hits < total * 0.8 or windows_hits == 0:
                raise AssertionError(f"{name} did not stay active in HID and Windows")
    finally:
        post("/api/release", {})
        device.close()


if __name__ == "__main__":
    main()
