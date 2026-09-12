/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "LocalHbm.xiaomi_sm8650"

#include "LocalHbm.h"

#include <android-base/logging.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <thread>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
// UAPI: Qualcomm/Xiaomi display-drivers/include/uapi/display/drm/mi_disp.h.
struct DisplayRequest {
    uint32_t flags;
    uint32_t displayId;
    uint32_t value;
};
struct DisplayEvent {
    int32_t displayId;
    uint32_t type;
    uint32_t length;
};
struct DisplayFps {
    uint32_t flags;
    uint32_t displayId;
    uint32_t fps;
    uint32_t mode[4];
};
static_assert(sizeof(DisplayFps) == 28);
constexpr unsigned long kGetFps = _IOR('D', 0x06, DisplayFps);
constexpr unsigned long kRegisterEvent = _IOW('D', 0x07, DisplayRequest);
constexpr unsigned long kSetLocalHbm = _IOW('D', 0x0e, DisplayRequest);
constexpr uint32_t kFodEvent = 2;
constexpr uint32_t kUiReady = 1 << 3;
constexpr uint32_t kWhite1000Nit = 2;
constexpr uint32_t kFingerUp = 0;
constexpr uint32_t kAuthStop = 1;
}  // namespace

bool LocalHbm::openDisplay() {
    if (mFd.get() >= 0) return true;
    mFd.reset(TEMP_FAILURE_RETRY(open("/dev/mi_display/disp_feature",
                                    O_RDWR | O_NONBLOCK | O_CLOEXEC)));
    if (mFd.get() < 0) {
        PLOG(ERROR) << "Unable to open display event interface";
        return false;
    }
    DisplayRequest event{0, 0, kFodEvent};
    if (ioctl(mFd.get(), kRegisterEvent, &event) < 0) {
        PLOG(ERROR) << "Unable to subscribe to FOD readiness";
        mFd.reset();
        return false;
    }
    return true;
}

bool LocalHbm::request(unsigned int value) {
    DisplayRequest request{0, 0, value};
    if (ioctl(mFd.get(), kSetLocalHbm, &request) < 0) {
        PLOG(ERROR) << "Unable to request local HBM " << value;
        return false;
    }
    return true;
}

bool LocalHbm::waitForRefreshRate(unsigned int cancellation) {
    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + 80ms;
    DisplayFps fps{};
    while (cancellation == mCancellation.load()) {
        if (ioctl(mFd.get(), kGetFps, &fps) < 0) {
            PLOG(WARNING) << "Unable to query panel refresh rate; using readiness event";
            return true;
        }
        if (fps.fps >= 120 || std::chrono::steady_clock::now() >= deadline) {
            LOG(INFO) << "FOD refresh rate " << fps.fps << " Hz after "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start).count()
                      << " ms";
            // Keep the driver's actual frame-based readiness delay if high refresh is
            // unavailable, for example while display policy is restricting the mode.
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

void LocalHbm::stop() {
    // An off request also supersedes an on request queued while the panel is asleep.
    // Both UAPI states turn local HBM off. Alternate them so that even an on request
    // cancelled before execution yields an off event (the driver suppresses repeats).
    const auto off = mLastOff == kFingerUp ? kAuthStop : kFingerUp;
    if (mRequested && request(off)) {
        mLastOff = off;
        mRequested = false;
        mAwaitingOff = true;
    }
}

bool LocalHbm::enable() {
    using namespace std::chrono_literals;
    const auto cancellation = mCancellation.load();
    std::lock_guard<std::mutex> lock(mMutex);
    if (cancellation != mCancellation.load() || !openDisplay()) return false;
    if (mRequested) return false;

    char buffer[512];
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + 650ms;
    // Wait for the previous off acknowledgement before queuing another on request.
    // The driver coalesces queued requests; skipping this boundary could reuse old readiness.
    if (!mAwaitingOff) {
        while (TEMP_FAILURE_RETRY(read(mFd.get(), buffer, sizeof(buffer))) > 0) {}
        if (!waitForRefreshRate(cancellation)) return false;
        if (!request(kWhite1000Nit)) return false;
        mRequested = true;
    }

    while (cancellation == mCancellation.load() && std::chrono::steady_clock::now() < deadline) {
        // Short bounded poll permits a concurrent finger-up/cancel to interrupt this wait.
        pollfd eventFd{mFd.get(), POLLIN, 0};
        const int result = TEMP_FAILURE_RETRY(poll(&eventFd, 1, 20));
        if (result < 0 || (eventFd.revents & (POLLERR | POLLHUP | POLLNVAL))) break;
        if (!(eventFd.revents & POLLIN)) continue;
        const auto size = TEMP_FAILURE_RETRY(read(mFd.get(), buffer, sizeof(buffer)));
        if (size < 0) break;
        for (size_t offset = 0; offset + sizeof(DisplayEvent) <= static_cast<size_t>(size);) {
            DisplayEvent event;
            memcpy(&event, buffer + offset, sizeof(event));
            if (event.length < sizeof(event) || event.length > static_cast<size_t>(size) - offset) {
                LOG(ERROR) << "Malformed display event";
                stop();
                return false;
            }
            if (event.displayId == 0 && event.type == kFodEvent &&
                event.length >= sizeof(event) + sizeof(uint32_t)) {
                uint32_t state;
                memcpy(&state, buffer + offset + sizeof(event), sizeof(state));
                if (mAwaitingOff && state == 0) {
                    mAwaitingOff = false;
                    if (cancellation != mCancellation.load()) break;
                    if (!waitForRefreshRate(cancellation)) return false;
                    if (!request(kWhite1000Nit)) return false;
                    mRequested = true;
                    // Only readiness produced after this new request is eligible.
                    break;
                }
                if (!mAwaitingOff && mRequested && (state & kUiReady) &&
                    cancellation == mCancellation.load()) {
                    LOG(INFO) << "Panel ready after "
                              << std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start).count()
                              << " ms";
                    return true;
                }
            }
            offset += event.length;
        }
    }
    LOG(WARNING) << "Panel readiness cancelled or timed out";
    stop();
    return false;
}

void LocalHbm::disable() {
    ++mCancellation;
    std::lock_guard<std::mutex> lock(mMutex);
    stop();
}
