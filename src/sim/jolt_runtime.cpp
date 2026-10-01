#include "sim/jolt_runtime.h"

#include "sim/jolt_config.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

namespace rally::sim {

bool jolt_abi_matches() { return JPH::VerifyJoltVersionID(); }

JoltRuntime::JoltRuntime() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    // RegisterTypes también comprueba JPH_VERSION_ID y aborta si no coincide.
    JPH::RegisterTypes();
}

JoltRuntime::~JoltRuntime() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

} // namespace rally::sim
