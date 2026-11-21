#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

namespace knoxspeak {

class UiDispatcher {
  public:
    UiDispatcher();  // must be constructed on the TS GUI thread (from ts3plugin_init)
    ~UiDispatcher(); // same thread; pending work is dropped
    UiDispatcher(const UiDispatcher&) = delete;
    UiDispatcher& operator=(const UiDispatcher&) = delete;

    // thread-safe
    void post(std::function<void()> fn);

    [[nodiscard]] bool ok() const { return hwnd_ != nullptr; }

    void drain();

  private:
    void* hwnd_ = nullptr; // HWND
    std::atomic<bool> closing_{false};
    std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
};

} // namespace knoxspeak
