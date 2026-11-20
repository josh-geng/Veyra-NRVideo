// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/Ps5KbmCapture.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaObject>

#include <future>

#include "veyra/Log.h"

namespace veyra::ui {
namespace {
constexpr UINT WM_KBM_REGISTER = WM_APP + 0x4B1; // wParam: 1 register, 0 remove
constexpr wchar_t RawClass[] = L"VeyraPs5KbmRawInput";
std::uint16_t resolveSided(UINT vk, UINT scan, bool extended) {
    switch (vk) {
    case VK_SHIFT: { const UINT sided = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX); return std::uint16_t(sided ? sided : VK_LSHIFT); }
    case VK_CONTROL: return extended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU: return extended ? VK_RMENU : VK_LMENU;
    default: return std::uint16_t(vk);
    }
}
bool registerDevices(HWND target, bool on) {
    RAWINPUTDEVICE devices[2]{};
    devices[0].usUsagePage = 0x01; devices[0].usUsage = 0x02; // mouse
    devices[1].usUsagePage = 0x01; devices[1].usUsage = 0x06; // keyboard
    for (auto& d : devices) {
        // INPUTSINK: delivered even when the UI thread is busy; foreground is checked per report.
        // No RIDEV_NOLEGACY: the UI still receives (and the filter swallows) legacy messages,
        // so releasing the capture never leaves Windows without keyboard or mouse messages.
        d.dwFlags = on ? (RIDEV_INPUTSINK | RIDEV_DEVNOTIFY) : RIDEV_REMOVE;
        d.hwndTarget = on ? target : nullptr;
    }
    return RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
}
} // namespace

Ps5KbmCapture::Ps5KbmCapture(std::shared_ptr<remoteplay::kbm::KbmEngine> engine, QObject* parent)
    : QObject(parent), engine_(std::move(engine)) {
    // Shared: the thread may still be inside set_value() when the constructor resumes.
    auto ready = std::make_shared<std::promise<HWND>>();
    auto created = ready->get_future();
    thread_ = std::thread([this, ready] {
        threadId_.store(GetCurrentThreadId());
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Ps5KbmCapture::rawProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = RawClass;
        RegisterClassExW(&wc); // may already exist after a restart of the capture object
        HWND hwnd = CreateWindowExW(0, RawClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, this);
        // Input handling must not queue behind decode, GPU submission or the UI.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        ready->set_value(hwnd);
        if (!hwnd) return;
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
        if (registered_.exchange(false)) registerDevices(nullptr, false);
        DestroyWindow(hwnd);
    });
    rawWindow_ = created.get();
    if (!rawWindow_) veyra::log::error("ps5-kbm", "raw-input window creation failed; keyboard/mouse control unavailable");
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && captured()) { capture(false); emit focusLost(); }
    });
}

Ps5KbmCapture::~Ps5KbmCapture() {
    capture(false);
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
    if (const DWORD id = threadId_.load()) PostThreadMessageW(id, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    engine_->setEnabled(false);
}

LRESULT CALLBACK Ps5KbmCapture::rawProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<Ps5KbmCapture*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_INPUT:
        self->onRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(hwnd, msg, wParam, lParam); // lets the system free the input
    case WM_INPUT_DEVICE_CHANGE:
        if (wParam == GIDC_REMOVAL && self->captured()) {
            // We do not know which keys that device held: release everything.
            veyra::log::warn("ps5-kbm", "input device removed while captured; all simulated inputs released");
            self->releaseFromRawThread(2);
        }
        return 0;
    case WM_KBM_REGISTER: {
        const bool on = wParam != 0;
        const bool ok = registerDevices(hwnd, on);
        self->registered_.store(on && ok);
        if (!ok) veyra::log::warn("ps5-kbm", std::string("RegisterRawInputDevices failed error=") + std::to_string(GetLastError()));
        self->wheelAccum_ = self->hwheelAccum_ = 0;
        self->ctrl_ = self->alt_ = self->shift_ = false;
        return ok ? 1 : 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void Ps5KbmCapture::onRawInput(HRAWINPUT handle) {
    alignas(8) BYTE buffer[sizeof(RAWINPUT) + 64];
    UINT size = sizeof(buffer);
    if (GetRawInputData(handle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (!captured_.load(std::memory_order_acquire)) return;
    const auto now = remoteplay::monotonic100ns();
    // Focus safety that does not depend on the UI thread: at most every 2 ms.
    if (now - lastForegroundCheck_ > 20000) {
        lastForegroundCheck_ = now;
        const HWND fg = GetForegroundWindow();
        const HWND own = window_.load(std::memory_order_acquire);
        if (!own || (fg != own && GetAncestor(fg, GA_ROOT) != own)) { releaseFromRawThread(1); return; }
    }
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
    if (raw->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;
        if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
            // Remote desktop, some tablets and VMs: no relative counts to translate.
            if (!warnedAbsolute_) { warnedAbsolute_ = true; veyra::log::warn("ps5-kbm", "absolute-position mouse reports ignored (remote desktop / tablet / VM); camera control needs a relative mouse"); }
        } else if (m.lLastX || m.lLastY) engine_->mouseMove(int(m.lLastX), int(m.lLastY), now);
        const USHORT f = m.usButtonFlags;
        using namespace remoteplay::kbm;
        struct Edge { USHORT down, up; InputCode code; };
        static constexpr Edge edges[] = {
            {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, code::MouseLeft},
            {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, code::MouseRight},
            {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, code::MouseMiddle},
            {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, code::MouseX1},
            {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, code::MouseX2}};
        for (const auto& e : edges) {
            if (f & e.down) engine_->key(e.code, true, now);
            if (f & e.up) engine_->key(e.code, false, now);
        }
        const auto notches = [&](int& accum, InputCode positive, InputCode negative) {
            accum += int(static_cast<SHORT>(m.usButtonData));
            // High-resolution wheels send fractions of 120; act on whole notches only.
            int up = 0, down = 0;
            while (accum >= WHEEL_DELTA) { accum -= WHEEL_DELTA; ++up; }
            while (accum <= -WHEEL_DELTA) { accum += WHEEL_DELTA; ++down; }
            if (up) engine_->wheel(positive, up, now);
            if (down) engine_->wheel(negative, down, now);
        };
        if (f & RI_MOUSE_WHEEL) notches(wheelAccum_, code::WheelUp, code::WheelDown);
        if (f & RI_MOUSE_HWHEEL) notches(hwheelAccum_, code::WheelRight, code::WheelLeft);
        return;
    }
    if (raw->header.dwType != RIM_TYPEKEYBOARD) return;
    const RAWKEYBOARD& k = raw->data.keyboard;
    if (k.VKey == 0 || k.VKey >= 0xFF) return;                  // overrun / fake key
    const bool up = (k.Flags & RI_KEY_BREAK) != 0;
    const bool e0 = (k.Flags & RI_KEY_E0) != 0;
    if (k.VKey == VK_SHIFT && e0) return;                         // synthetic shift around numpad navigation
    const std::uint16_t vk = resolveSided(k.VKey, k.MakeCode, e0);
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) ctrl_ = !up;
    if (vk == VK_LMENU || vk == VK_RMENU) alt_ = !up;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) shift_ = !up;
    if (!up && vk == 'Z' && ctrl_ && alt_ && shift_) { releaseFromRawThread(0); return; }
    engine_->key(vk, !up, now);
}

void Ps5KbmCapture::releaseFromRawThread(int reason) {
    if (!captured_.exchange(false)) return;
    engine_->setEnabled(false); // console sees neutral from the next submission, UI thread or not
    if (registered_.exchange(false)) registerDevices(nullptr, false);
    QMetaObject::invokeMethod(this, [this, reason] {
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        emit capturedChanged(false);
        if (reason == 0) emit releaseRequested();
        else if (reason == 1) emit focusLost();
        else emit deviceRemoved();
    }, Qt::QueuedConnection);
}

bool Ps5KbmCapture::capture(bool on) {
    if (on == captured()) return true;
    if (on) {
        const HWND own = window_.load();
        if (!own || !rawWindow_ || GetForegroundWindow() != own) return false;
        engine_->setEnabled(true);
        captured_.store(true, std::memory_order_release);
        lastForegroundCheck_ = 0;
        if (!SendMessageW(rawWindow_, WM_KBM_REGISTER, 1, 0)) {
            captured_.store(false); engine_->setEnabled(false);
            return false;
        }
        clipToWindow();
        SetCursor(nullptr);
        veyra::log::info("ps5-kbm", "keyboard/mouse captured for PS5 Remote Play");
    } else {
        captured_.store(false, std::memory_order_release);
        engine_->setEnabled(false);
        if (rawWindow_) SendMessageW(rawWindow_, WM_KBM_REGISTER, 0, 0);
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        veyra::log::info("ps5-kbm", "keyboard/mouse released; simulated controller neutral");
    }
    emit capturedChanged(on);
    return true;
}

void Ps5KbmCapture::armRebind(std::function<void(std::uint16_t)> onKey) { rebind_ = std::move(onKey); }

void Ps5KbmCapture::clipToWindow() {
    const HWND own = window_.load();
    RECT client{};
    if (!own || !GetClientRect(own, &client)) return;
    POINT tl{client.left, client.top}, br{client.right, client.bottom};
    ClientToScreen(own, &tl); ClientToScreen(own, &br);
    const RECT screen{tl.x, tl.y, br.x, br.y};
    ClipCursor(&screen);
}

bool Ps5KbmCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if (eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    const HWND own = window_.load();
    if (!msg->hwnd || !own || GetAncestor(msg->hwnd, GA_ROOT) != own) return false;
    if (!captured()) {
        if (rebind_ && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)) {
            const auto vk = resolveSided(UINT(msg->wParam), UINT((msg->lParam >> 16) & 0xFF), ((msg->lParam >> 24) & 1) != 0);
            auto done = std::move(rebind_); rebind_ = nullptr;
            done(vk);
            if (result) *result = 0;
            return true;
        }
        return false;
    }
    switch (msg->message) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR: case WM_SYSCHAR: case WM_DEADCHAR: case WM_SYSDEADCHAR: case WM_UNICHAR: case WM_IME_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK: case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_MOUSEMOVE:
        if (result) *result = 0;
        return true; // the console gets these through raw input; the player UI must not
    case WM_SYSCOMMAND:
        if ((msg->wParam & 0xFFF0) == SC_KEYMENU) { if (result) *result = 0; return true; }
        return false;
    case WM_SETCURSOR:
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE: case WM_MOVE: case WM_DISPLAYCHANGE: case WM_DPICHANGED:
        clipToWindow();
        return false;
    case WM_ACTIVATE:
        if (LOWORD(msg->wParam) == WA_INACTIVE) { capture(false); emit focusLost(); }
        return false;
    case WM_KILLFOCUS: {
        // Focus moving between Veyra's own child windows (video surface ↔ QML) is not a loss.
        const HWND next = reinterpret_cast<HWND>(msg->wParam);
        if (!next || GetAncestor(next, GA_ROOT) != own) { capture(false); emit focusLost(); }
        return false;
    }
    default:
        return false;
    }
}

} // namespace veyra::ui
