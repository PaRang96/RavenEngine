"""Select and place the stock host for the native-resolution benchmark through its launch options."""

import ctypes as c
from ctypes import wintypes as w
from dataclasses import dataclass


@dataclass(frozen=True)
class Display:
    handle: int
    name: str
    hz: int
    x: int
    y: int
    width: int
    height: int


class MonitorInfo(c.Structure):
    _fields_ = [("size", w.DWORD), ("bounds", w.RECT), ("work", w.RECT),
                ("flags", w.DWORD), ("device", w.WCHAR * 32)]


def fastest_display(user):
    gdi = c.windll.gdi32
    gdi.CreateDCW.argtypes = (w.LPCWSTR, w.LPCWSTR, w.LPCWSTR, w.LPVOID)
    gdi.CreateDCW.restype = w.HDC
    gdi.GetDeviceCaps.argtypes = (w.HDC, c.c_int)
    gdi.DeleteDC.argtypes = (w.HDC,)
    callback = c.WINFUNCTYPE(w.BOOL, w.HMONITOR, w.HDC, c.POINTER(w.RECT), w.LPARAM)
    user.EnumDisplayMonitors.argtypes = (w.HDC, c.POINTER(w.RECT), callback, w.LPARAM)
    user.GetMonitorInfoW.argtypes = (w.HMONITOR, c.POINTER(MonitorInfo))
    displays = []

    @callback
    def collect(handle, _dc, _rect, _data):
        info = MonitorInfo()
        info.size = c.sizeof(info)
        if user.GetMonitorInfoW(handle, c.byref(info)):
            dc = gdi.CreateDCW("DISPLAY", info.device, None, None)
            if dc:
                hz = gdi.GetDeviceCaps(dc, 116)  # VREFRESH
                gdi.DeleteDC(dc)
                left, top, right, bottom = (info.bounds.left, info.bounds.top, info.bounds.right, info.bounds.bottom)
                displays.append(Display(handle, info.device, hz, left, top, right - left, bottom - top))
        return True

    user.EnumDisplayMonitors(None, None, collect, 0)
    if not displays:
        raise RuntimeError("Could not find a Windows display for the benchmark")
    return max(displays, key=lambda display: display.hz)


def cases(display, selected, trial, user):
    user.MonitorFromWindow.argtypes = (w.HWND, w.DWORD)
    user.MonitorFromWindow.restype = w.HMONITOR

    def check_placement(app):
        window, client = w.RECT(), w.RECT()
        valid = (user.MonitorFromWindow(app.window, 0) == display.handle and
                 user.GetWindowRect(app.window, c.byref(window)) and
                 user.GetClientRect(app.window, c.byref(client)))
        if not valid or (window.left, window.top, client.right, client.bottom) != (
                display.x, display.y, display.width, display.height):
            raise RuntimeError("The benchmark window must fill the selected display at native resolution")

    placement = ["--borderless", *(str(value) for value in (display.x, display.y, display.width, display.height))]
    for setting in selected if trial % 2 else reversed(selected):
        adapter, presentation = setting.split("-", 1)
        # Auto and nvidia use the default discrete-first selection; intel asks for the integrated GPU.
        gpu = ["--gpu", "integrated"] if adapter == "intel" else []
        yield f"{trial}-{setting}", [*gpu, "--present", presentation, *placement], check_placement
