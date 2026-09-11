#include <memory>

class C2ParamReflector;

namespace android {

class FilterWrapper {
public:
    std::shared_ptr<C2ParamReflector> getParamReflector();
};

// Match FilterWrapperStub: no filter-specific reflector is available.
// A void stub leaves the caller's shared_ptr return storage uninitialized.
std::shared_ptr<C2ParamReflector> FilterWrapper::getParamReflector() {
    return nullptr;
}

}  // namespace android
