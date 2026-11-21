#include "plugin/ui_dispatcher.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>

using knoxspeak::UiDispatcher;

namespace {
void pumpUntil(const std::atomic<bool>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    MSG msg;
    while (!done && std::chrono::steady_clock::now() < deadline) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace

TEST_CASE("work posted from another thread runs on the dispatcher's thread, in order") {
    UiDispatcher ui;
    REQUIRE(ui.ok());
    const DWORD mainThread = ::GetCurrentThreadId();

    std::vector<int> order;
    std::atomic<bool> wrongThread{false}, done{false};
    std::thread worker([&] {
        for (int i = 0; i < 100; ++i) {
            ui.post([&, i] {
                wrongThread = wrongThread || ::GetCurrentThreadId() != mainThread;
                order.push_back(i); // only touched on the main thread
                if (i == 99) {
                    done = true;
                }
            });
        }
    });
    pumpUntil(done);
    worker.join();

    REQUIRE(done);
    CHECK_FALSE(wrongThread);
    REQUIRE(order.size() == 100);
    for (int i = 0; i < 100; ++i) {
        CHECK(order[i] == i);
    }
}

TEST_CASE("pending work is dropped when the dispatcher is destroyed") {
    std::atomic<bool> ran{false};
    {
        UiDispatcher ui;
        ui.post([&] { ran = true; });
    } // destroyed before any message pump runs
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::DispatchMessageW(&msg);
    }
    CHECK_FALSE(ran);
}
