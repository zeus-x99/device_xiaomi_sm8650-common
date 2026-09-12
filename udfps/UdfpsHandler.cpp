/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.xiaomi_sm8650"

#include <aidl/android/hardware/biometrics/fingerprint/BnFingerprint.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <thread>

#include "UdfpsHandler.h"

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 1
#define PARAM_NIT_NONE 0

#define COMMAND_FOD_PRESS_STATUS 1
#define PARAM_FOD_PRESSED 1
#define PARAM_FOD_RELEASED 0

#define FOD_STATUS_PATH "/sys/class/touch/touch_dev/fod_press_status"
#define FOD_STATUS_OFF 0
#define FOD_STATUS_ON 1

#define DISP_PARAM_PATH "/sys/devices/virtual/mi_display/disp_feature/disp-DSI-0/disp_param"
#define DISP_PARAM_LOCAL_HBM_MODE "9"
#define DISP_PARAM_LOCAL_HBM_OFF "0"
#define DISP_PARAM_LOCAL_HBM_ON "1"

#define FINGERPRINT_ACQUIRED_VENDOR 7

using ::aidl::android::hardware::biometrics::fingerprint::AcquiredInfo;

namespace {

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

static bool setLocalHbm(bool enabled) {
    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + 500ms;
    const std::string value = std::string(DISP_PARAM_LOCAL_HBM_MODE) + " " +
            (enabled ? DISP_PARAM_LOCAL_HBM_ON : DISP_PARAM_LOCAL_HBM_OFF);

    // A doze pulse can reach the fingerprint HAL before the panel is initialized.
    // Retry that transient driver error, and never announce HBM readiness on failure.
    while (true) {
        android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(DISP_PARAM_PATH, O_WRONLY | O_CLOEXEC)));
        if (fd.get() < 0) {
            PLOG(ERROR) << "Unable to open local HBM control";
            return false;
        }
        const auto written = TEMP_FAILURE_RETRY(write(fd.get(), value.data(), value.size()));
        const int error = errno;
        if (written == static_cast<ssize_t>(value.size())) {
            if (enabled) {
                LOG(INFO) << "Local HBM enabled after "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start).count()
                          << " ms";
            }
            return true;
        }
        if (!enabled || written >= 0 || error != ENODEV ||
            std::chrono::steady_clock::now() >= deadline) {
            LOG(ERROR) << "Unable to set local HBM to " << enabled
                       << ": written=" << written << ", errno=" << error;
            return false;
        }
        std::this_thread::sleep_for(20ms);
    }
}

}  // anonymous namespace

class XiaomiSM8650UdfpsHander : public UdfpsHandler {
  public:
    void init(fingerprint_device_t* device) {
        mDevice = device;
    }

    void onFingerDown(uint32_t /*x*/, uint32_t /*y*/, float /*minor*/, float /*major*/) {
        LOG(INFO) << __func__;
        setFingerDown(true);
    }

    void onFingerUp() {
        LOG(INFO) << __func__;
        setFingerDown(false);
    }

    void onAcquired(int32_t result, int32_t vendorCode) {
        LOG(INFO) << __func__ << " result: " << result << " vendorCode: " << vendorCode;
        if (result != FINGERPRINT_ACQUIRED_VENDOR) {
            setFingerDown(false);
            if (static_cast<AcquiredInfo>(result) == AcquiredInfo::GOOD) {
                setFodStatus(FOD_STATUS_OFF);
            }
        } else if (vendorCode == 21 || vendorCode == 23) {
            /*
             * vendorCode = 21 waiting for fingerprint authentication
             * vendorCode = 23 waiting for fingerprint enroll
             */
            setFodStatus(FOD_STATUS_ON);
        } else if (vendorCode == 44) {
            /*
             * vendorCode = 44 fingerprint scan failed
             */
            setFingerDown(false);
        }
    }

    void cancel() {
        LOG(INFO) << __func__;
        setFingerDown(false);
        setFodStatus(FOD_STATUS_OFF);
    }

  private:
    fingerprint_device_t* mDevice;

    void setFodStatus(int value) {
        set(FOD_STATUS_PATH, value);
    }

    void setFingerDown(bool pressed) {
        if (pressed) {
            if (!setLocalHbm(true)) {
                return;
            }
            mDevice->extCmd(mDevice, COMMAND_NIT, PARAM_NIT_FOD);
            mDevice->extCmd(mDevice, COMMAND_FOD_PRESS_STATUS, PARAM_FOD_PRESSED);
        } else {
            mDevice->extCmd(mDevice, COMMAND_NIT, PARAM_NIT_NONE);
            setLocalHbm(false);
        }
    }
};

static UdfpsHandler* create() {
    return new XiaomiSM8650UdfpsHander();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
        .create = create,
        .destroy = destroy,
};
