// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "DolbyCodec2Service"

#include <csignal>
#include <memory>

#include <binder/ProcessState.h>
#include <codec2/hidl/1.0/ComponentStore.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

namespace android {
// Factory exported by the proprietary Dolby component-store library.
std::shared_ptr<C2ComponentStore> GetCodec2DolbyComponentStore();
}  // namespace android

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    android::ProcessState::initWithDriver("/dev/vndbinder")->startThreadPool();
    android::hardware::configureRpcThreadpool(8, true);

    using android::hardware::media::c2::V1_0::utils::ComponentStore;
    auto vendorStore = android::GetCodec2DolbyComponentStore();
    if (!vendorStore) {
        ALOGE("Dolby component-store factory returned null");
        return 1;
    }

    // Compile the allocation and constructor against the same platform headers.
    // The old prebuilt allocates only 264 bytes, which is too small on this branch.
    android::sp<ComponentStore> store = android::sp<ComponentStore>::make(vendorStore);
    if (store->registerAsService("default1") != android::OK) {
        ALOGE("Failed to register Dolby component store");
        return 1;
    }
    ALOGI("Registered Dolby component store (wrapper size=%zu)", sizeof(ComponentStore));
    android::hardware::joinRpcThreadpool();
    return 1;
}
