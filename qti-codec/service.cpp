// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "QtiCodec2Service"

#include <csignal>
#include <memory>

#include <binder/ProcessState.h>
#ifdef QTI_VIDEO_CODEC
#include <codec2/hidl/1.2/ComponentStore.h>
#else
#include <codec2/hidl/1.0/ComponentStore.h>
#endif
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <minijail.h>

// The vendor factory's get-store vtable entry is the same function as this
// exported static getter. Both return shared_ptr<C2ComponentStore> via x8.
#ifdef QTI_VIDEO_CODEC
std::shared_ptr<C2ComponentStore> GetQtiStore()
        asm("_ZN3qc217QC2ComponentStore3GetEv");
using android::hardware::media::c2::V1_2::utils::ComponentStore;
static constexpr char kBasePolicy[] = "/vendor/etc/seccomp_policy/codec2.vendor.base-arm64.policy";
static constexpr char kExtPolicy[] = "/vendor/etc/seccomp_policy/codec2.vendor.ext-arm64.policy";
static constexpr char kInstance[] = "default";
#else
std::shared_ptr<C2ComponentStore> GetQtiStore()
        asm("_ZN8qc2audio17QC2ComponentStore3GetEv");
using android::hardware::media::c2::V1_0::utils::ComponentStore;
static constexpr char kBasePolicy[] = "/vendor/etc/seccomp_policy/c2audio.vendor.base-arm64.policy";
static constexpr char kExtPolicy[] = "/vendor/etc/seccomp_policy/c2audio.vendor.ext-arm64.policy";
static constexpr char kInstance[] = "default2";
#endif

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    android::SetUpMinijail(kBasePolicy, kExtPolicy);
    android::ProcessState::initWithDriver("/dev/vndbinder")->startThreadPool();
    android::hardware::configureRpcThreadpool(8, true);

    auto vendorStore = GetQtiStore();
    if (!vendorStore) {
        ALOGE("Vendor component-store factory returned null for %s", kInstance);
        return 1;
    }
    auto store = android::sp<ComponentStore>::make(vendorStore);
    if (store->registerAsService(kInstance) != android::OK) {
        ALOGE("Failed to register component store %s", kInstance);
        return 1;
    }
    ALOGI("Registered component store %s (wrapper size=%zu)", kInstance, sizeof(ComponentStore));
    android::hardware::joinRpcThreadpool();
    return 1;
}
