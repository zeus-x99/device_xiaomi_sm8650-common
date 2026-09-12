/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <android-base/unique_fd.h>
#include <atomic>
#include <mutex>

// Xiaomi display driver requests and readiness are separate operations.
class LocalHbm {
  public:
    bool enable();
    void disable();

  private:
    bool openDisplay();
    bool request(unsigned int value);
    bool waitForRefreshRate(unsigned int cancellation);
    void stop();

    std::mutex mMutex;
    std::atomic<unsigned int> mCancellation{0};
    android::base::unique_fd mFd;
    bool mRequested = false;
    bool mAwaitingOff = false;
    unsigned int mLastOff = 0;
};
