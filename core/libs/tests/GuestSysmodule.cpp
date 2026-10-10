#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

extern "C" {
int APS5_VABI sceSysmoduleIsLoaded(std::uint16_t id);
int APS5_VABI sceSysmoduleLoadModule(std::uint16_t id);
int APS5_VABI sceSysmoduleUnloadModule(std::uint16_t id);
int APS5_VABI sceSysmoduleLoadModuleInternal(std::uint32_t id);
int APS5_VABI sceSysmoduleUnloadModuleInternal(std::uint32_t id);
int APS5_VABI sceSysmoduleGetModuleHandleInternal(std::uint32_t id, std::int32_t* handle);
int APS5_VABI sceKernelDlsym(std::int32_t handle, const char* symbol, void** addr);
}

namespace {

void Require(bool value) { if (!value) std::abort(); }

template <typename TFunction>
bool ThrowsRuntimeError(TFunction function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

constexpr std::uint16_t kFiberModuleId = 0x0006;
constexpr std::uint16_t kUltModuleId = 0x0007;
constexpr int kModuleUnloaded = static_cast<int>(0x805A1001);

}

int main() {
    Require(sceSysmoduleIsLoaded(kFiberModuleId) == kModuleUnloaded);
    Require(sceSysmoduleLoadModuleInternal(kFiberModuleId) == 0);
    Require(sceSysmoduleIsLoaded(kFiberModuleId) == 0);
    Require(sceSysmoduleLoadModuleInternal(kFiberModuleId) == 0);
    Require(sceSysmoduleUnloadModuleInternal(kFiberModuleId) == 0);
    Require(sceSysmoduleIsLoaded(kFiberModuleId) == 0);
    Require(sceSysmoduleUnloadModuleInternal(kFiberModuleId) == 0);
    Require(sceSysmoduleIsLoaded(kFiberModuleId) == kModuleUnloaded);
    Require(sceSysmoduleUnloadModuleInternal(kFiberModuleId) == kModuleUnloaded);

    Require(sceSysmoduleIsLoaded(kUltModuleId) == kModuleUnloaded);
    Require(sceSysmoduleUnloadModule(kUltModuleId) == kModuleUnloaded);
    Require(sceSysmoduleLoadModule(kUltModuleId) == 0);
    Require(sceSysmoduleIsLoaded(kUltModuleId) == 0);
    Require(sceSysmoduleUnloadModule(kUltModuleId) == 0);
    Require(sceSysmoduleIsLoaded(kUltModuleId) == kModuleUnloaded);

    std::int32_t handle = -1;
    Require(sceSysmoduleGetModuleHandleInternal(kFiberModuleId, &handle) == kModuleUnloaded);
    Require(handle == -1);
    Require(ThrowsRuntimeError([] { sceSysmoduleGetModuleHandleInternal(0, nullptr); }));
    Require(ThrowsRuntimeError([&] { sceSysmoduleGetModuleHandleInternal(0, &handle); }));
    Require(ThrowsRuntimeError([] { sceSysmoduleGetModuleHandleInternal(kFiberModuleId, nullptr); }));
    Require(ThrowsRuntimeError([&] { sceSysmoduleGetModuleHandleInternal(0x00ffffffu, &handle); }));

    Require(sceSysmoduleLoadModule(kFiberModuleId) == 0);
    Require(sceSysmoduleGetModuleHandleInternal(kFiberModuleId, &handle) == 0);
    Require(handle >= 0);
    std::int32_t again = -1;
    Require(sceSysmoduleGetModuleHandleInternal(kFiberModuleId, &again) == 0);
    Require(again == handle);
    void* symbol = nullptr;
    Require(sceKernelDlsym(handle, "sceFiberOptParamInitialize", &symbol) == 0);
    Require(symbol != nullptr);
    Require(sceSysmoduleUnloadModule(kFiberModuleId) == 0);
    Require(sceSysmoduleGetModuleHandleInternal(kFiberModuleId, &handle) == kModuleUnloaded);
}
