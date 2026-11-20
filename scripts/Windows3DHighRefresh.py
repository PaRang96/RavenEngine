"""Build and place a disposable host for the native-resolution benchmark."""

from contextlib import contextmanager
import ctypes as c
from ctypes import wintypes as w
from dataclasses import dataclass
import os


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


@contextmanager
def probe_environment(values):
    previous = {name: os.environ.get(name) for name in values}
    os.environ.update({name: str(value) for name, value in values.items()})
    try:
        yield
    finally:
        for name, value in previous.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


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

    for setting in selected if trial % 2 else reversed(selected):
        adapter, presentation = setting.split("-", 1)
        yield f"{trial}-{setting}", probe_environment({
            "RAVEN_SCALE_ADAPTER": adapter, "RAVEN_SCALE_PRESENT": presentation,
            "RAVEN_SCALE_WIDTH": display.width, "RAVEN_SCALE_HEIGHT": display.height,
            "RAVEN_SCALE_X": display.x, "RAVEN_SCALE_Y": display.y,
        }), check_placement


def edit_snapshot(source):
    # These switches belong to the disposable benchmark host until the production presentation options land.
    private = source / "RavenEngine/Private"

    def replace(relative, edits):
        path = private / relative
        code = path.read_text(encoding="utf-8")
        for old, new in edits:
            if code.count(old) != 1:
                raise RuntimeError(f"Update the high-refresh benchmark injection for {relative}")
            code = code.replace(old, new)
        path.write_text(code, encoding="utf-8", newline="\n")

    replace("Core/Application.cpp", [
        ("\t\t\tstd::this_thread::sleep_until(frameStart + frameBudget);",
         "\t\t\tif (!drawable)\n\t\t\t\tstd::this_thread::sleep_until(frameStart + frameBudget);"),
    ])
    replace("Renderer/Vulkan/VulkanSwapchain.cpp", [
        ("#include <array>", "#include <array>\n#include <cstdlib>\n#include <iostream>\n#include <string_view>"),
        ("\t\tcreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;", '''\t\tconst auto* requested = std::getenv("RAVEN_SCALE_PRESENT");
\t\tconst bool mailbox = requested && std::string_view(requested) == "mailbox";
\t\tcreateInfo.presentMode = mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
\t\tif (std::find(modes.begin(), modes.end(), createInfo.presentMode) == modes.end())
\t\t\tthrow std::runtime_error("Requested benchmark presentation mode is unavailable");
\t\tstd::clog << "[scale] Present " << (mailbox ? "mailbox" : "fifo") << '\\n';'''),
    ])
    replace("Renderer/Vulkan/VulkanPhysicalDevice.cpp", [
        ("#include <cstring>", "#include <cstring>\n#include <cstdlib>\n#include <string_view>"),
        ("\t\tfor (VkPhysicalDevice device : devices)\n\t\t{", '''\t\tconst auto* option = std::getenv("RAVEN_SCALE_ADAPTER");
\t\tconst std::string_view requested = option ? option : "auto";
\t\tconst auto vendor = requested == "intel" ? 0x8086u : requested == "nvidia" ? 0x10deu : 0u;
\t\tfor (VkPhysicalDevice device : devices)
\t\t{
\t\t\tVkPhysicalDeviceProperties properties{};
\t\t\tvkGetPhysicalDeviceProperties(device, &properties);
\t\t\tif (vendor && properties.vendorID != vendor)
\t\t\t\tcontinue;'''),
    ])
    replace("Platform/Windows/Win32Window.cpp", [
        ("#include <cstdint>", "#include <cstdint>\n#include <cstdlib>"),
        ("\t\tm_Handle = CreateWindowExW(\n\t\t\t0, ClassName, title.c_str(), WS_OVERLAPPEDWINDOW,\n"
         "\t\t\tCW_USEDEFAULT, CW_USEDEFAULT,\n\t\t\tbounds.right - bounds.left,\n\t\t\tbounds.bottom - bounds.top,",
         '''\t\tconst auto number = [](const char* name)
\t\t{
\t\t\tconst auto* value = std::getenv(name);
\t\t\tif (!value) throw std::runtime_error("Missing benchmark window bounds");
\t\t\treturn std::atoi(value);
\t\t};
\t\tm_Handle = CreateWindowExW(
\t\t\tWS_EX_TOPMOST, ClassName, title.c_str(), WS_POPUP,
\t\t\tnumber("RAVEN_SCALE_X"), number("RAVEN_SCALE_Y"),
\t\t\tnumber("RAVEN_SCALE_WIDTH"), number("RAVEN_SCALE_HEIGHT"),'''),
    ])
