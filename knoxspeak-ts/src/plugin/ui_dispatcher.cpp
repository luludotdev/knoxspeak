#include "plugin/ui_dispatcher.hpp"

#include <windows.h>

namespace knoxspeak {
namespace {

constexpr UINT kDrainMessage = WM_APP + 0x4B53; // "KS"
constexpr wchar_t kClassName[] = L"knoxspeak.UiDispatcher";

HINSTANCE thisModule() {
    HMODULE module = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&thisModule), &module);
    return module;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kDrainMessage) {
        if (auto* self = reinterpret_cast<UiDispatcher*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
            self->drain();
        }
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

UiDispatcher::UiDispatcher() {
    const HINSTANCE inst = thisModule();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc); // fails harmlessly if a previous load already registered it

    HWND hwnd = ::CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (hwnd) {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        hwnd_ = hwnd;
    }
}

UiDispatcher::~UiDispatcher() {
    closing_ = true;
    if (hwnd_) {
        ::SetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_USERDATA, 0);
        ::DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
    ::UnregisterClassW(kClassName, thisModule());
    std::scoped_lock lock(mutex_);
    queue_.clear();
}

void UiDispatcher::post(std::function<void()> fn) {
    if (closing_ || !hwnd_) {
        return;
    }
    bool wasEmpty = false;
    {
        std::scoped_lock lock(mutex_);
        wasEmpty = queue_.empty();
        queue_.push_back(std::move(fn));
    }
    if (wasEmpty) {
        // coalesced: one wake up per batch
        ::PostMessageW(static_cast<HWND>(hwnd_), kDrainMessage, 0, 0);
    }
}

void UiDispatcher::drain() {
    std::deque<std::function<void()>> batch;
    {
        std::scoped_lock lock(mutex_);
        batch.swap(queue_);
    }
    for (auto& fn : batch) {
        if (closing_) {
            return;
        }
        try {
            fn();
        } catch (...) {
            // never let exceptions hit the main event loop
        }
    }
}

} // namespace knoxspeak
