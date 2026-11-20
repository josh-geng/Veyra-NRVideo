// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Keyboard-and-mouse capture for PS5 Remote Play (Win32 shim around kbm::KbmEngine).
//
// Input path: a dedicated, high-priority raw-input thread owns a message-only window
// registered for mouse and keyboard raw input (RIDEV_INPUTSINK + RIDEV_DEVNOTIFY). It
// stamps each report with the steady clock and hands it to the engine; the Remote Play
// session owner samples the engine right before every controller submission. The Qt
// UI thread therefore never sits between a mouse report and the console.
//
// Raw input gives hardware counts before Windows pointer acceleration ("Enhance pointer
// precision" does not apply) and independent of cursor position, clipping or DPI scaling.
//
// While captured, the UI thread's native event filter swallows the legacy keyboard and
// mouse messages of Veyra's window (so Space does not pause the player and clicks do not
// open menus), hides the cursor and confines it to the window.
//
// Release paths, any of which neutralizes every simulated input at once:
//   Ctrl+Alt+Shift+Z, focus loss (Alt+Tab, Win key, another window), WM_ACTIVATE/KILLFOCUS,
//   application deactivation, input device removal, session end, and the destructor.
// Not captured: Win key, Alt+Tab, Ctrl+Alt+Del and other system shortcuts (Windows keeps them).
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "veyra/remoteplay/kbm/KbmEngine.h"

namespace veyra::ui {

class Ps5KbmCapture final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit Ps5KbmCapture(std::shared_ptr<remoteplay::kbm::KbmEngine> engine, QObject* parent = nullptr);
    ~Ps5KbmCapture() override;

    void setWindow(HWND topLevel) { window_.store(topLevel, std::memory_order_release); }
    bool captured() const { return captured_.load(std::memory_order_acquire); }
    // false when it cannot start (no window, window not in the foreground, raw input refused).
    bool capture(bool on);
    // Next keyboard key pressed in Veyra's window (while not captured) is reported instead of
    // reaching the UI. Mouse buttons are reported by the QML capture area, not here.
    void armRebind(std::function<void(std::uint16_t)> onKey);
    void cancelRebind() { rebind_ = nullptr; }
    bool rebindArmed() const { return bool(rebind_); }
    bool rawInputAvailable() const { return rawWindow_ != nullptr; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void capturedChanged(bool captured);
    void releaseRequested();          // Ctrl+Alt+Shift+Z (capture is already released)
    void focusLost();                 // released because another window took the foreground
    void deviceRemoved();             // released because a keyboard or mouse was disconnected

private:
    static LRESULT CALLBACK rawProc(HWND, UINT, WPARAM, LPARAM);
    void rawThread();
    void onRawInput(HRAWINPUT);
    void releaseFromRawThread(int reason); // 0 hotkey, 1 focus, 2 device
    void clipToWindow();

    std::shared_ptr<remoteplay::kbm::KbmEngine> engine_;
    std::atomic<HWND> window_{nullptr};
    std::atomic<bool> captured_{false};
    std::function<void(std::uint16_t)> rebind_;
    // Raw-input thread state. Members below are touched only by that thread unless atomic.
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    HWND rawWindow_ = nullptr;
    std::atomic<bool> registered_{false};
    std::int64_t lastForegroundCheck_ = 0;
    int wheelAccum_ = 0, hwheelAccum_ = 0;
    bool ctrl_ = false, alt_ = false, shift_ = false, warnedAbsolute_ = false;
};

} // namespace veyra::ui
