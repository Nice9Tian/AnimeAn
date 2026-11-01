"""Save the client area of a running top-level window as PNG.

    py tests/ui_pixel/capture_window.py <pid> <out.png> [--screen] [--dismiss]

Uses PrintWindow with PW_RENDERFULLCONTENT, so GL and WebView2 surfaces are
included and the window need not be in front. Qt and Tauri go through this
same path, so the two baselines differ only by what the windows drew.

PrintWindow sometimes hands back a stale GL surface for QOpenGLWidget. --screen
brings the window to the front and copies the screen instead.

Both builds open on the New Project dialog. --dismiss presses Escape to cancel
it first, and only if this process really has the foreground, so the key can
never land in whatever else is on the desktop. The Qt window repaints nothing
until the pointer next touches it after the dialog closes, so the Qt baseline
is taken with PrintWindow while the dialog is up; cancelling leaves the same
state, down to the status bar text.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

from PIL import Image, ImageGrab

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.SetProcessDPIAware()

PW_CLIENTONLY = 1
PW_RENDERFULLCONTENT = 2
BI_RGB = 0
DIB_RGB_COLORS = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
        ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
    ]


def find_window(pid):
    """Largest visible top-level window owned by pid."""
    found = []
    EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def visit(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            rect = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(rect))
            found.append((rect.right * rect.bottom, hwnd, rect.right, rect.bottom))
        return True

    user32.EnumWindows(EnumProc(visit), 0)
    return max(found) if found else None


def capture(hwnd, width, height):
    window_dc = user32.GetDC(hwnd)
    memory_dc = gdi32.CreateCompatibleDC(window_dc)
    bitmap = gdi32.CreateCompatibleBitmap(window_dc, width, height)
    gdi32.SelectObject(memory_dc, bitmap)
    user32.PrintWindow(hwnd, memory_dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT)
    header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB)
    pixels = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(memory_dc, bitmap, 0, height, pixels, ctypes.byref(header), DIB_RGB_COLORS)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(memory_dc)
    user32.ReleaseDC(hwnd, window_dc)
    return Image.frombuffer("RGBA", (width, height), pixels, "raw", "BGRA", 0, 1).convert("RGB")


def press_escape(hwnd, pid):
    VK_ESCAPE, KEYUP = 0x1B, 2
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.5)
    owner = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(owner))
    if owner.value != pid:
        sys.exit(f"foreground is pid {owner.value}, not {pid}: not sending Escape")
    user32.keybd_event(VK_ESCAPE, 0, 0, 0)
    user32.keybd_event(VK_ESCAPE, 0, KEYUP, 0)
    time.sleep(2.0)


def capture_screen(hwnd, width, height):
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.8)
    origin = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return ImageGrab.grab((origin.x, origin.y, origin.x + width, origin.y + height)).convert("RGB")


def main():
    pid, out = int(sys.argv[1]), sys.argv[2]
    window = find_window(pid)
    if window is None:
        sys.exit(f"no visible window for pid {pid}")
    _, hwnd, width, height = window
    if "--dismiss" in sys.argv:
        press_escape(hwnd, pid)
    grab = capture_screen if "--screen" in sys.argv else capture
    grab(hwnd, width, height).save(out)
    print(f"{out}: {width}x{height}")


if __name__ == "__main__":
    main()
