#include "arm_ir_rig.h"

#include <cstdio>

namespace {

void RunCase(bool verify, uint32_t instruction, bool write) {
    cerfengine::ArmFeatures features{};
    features.arch_v7 = true;
    features.base_restored_abort_model = true;
    ArmIrRig rig(verify, features);
    rig.Host().AbortMmio();
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = 0x12345678u;
    initial.gprs[1] = kRigMmioPa;
    rig.Run({instruction, 0xE3A00000u, kRigBranchSelf, 0u, kRigBranchSelf}, initial);
    const auto& state = rig.State();
    TEST_CHECK(rig.System().ExternalAborts() == 1u);
    TEST_CHECK(rig.System().LastFaultVa() == kRigMmioPa);
    TEST_CHECK(rig.System().LastFaultWrite() == write);
    TEST_CHECK(state.cpsr.bits.mode == cerfengine::ArmMode::kAbort);
    TEST_CHECK(state.gprs[14] == 8u);
    TEST_CHECK(state.gprs[0] == initial.gprs[0]);
    TEST_CHECK(state.gprs[1] == initial.gprs[1]);
    TEST_CHECK(!rig.Host().MmioWritten());
}

}

int main() {
    for (bool verify : {false, true}) {
        RunCase(verify, 0xE5D10000u, false);
        RunCase(verify, 0xE5C10000u, true);
        RunCase(verify, 0xE1D100B0u, false);
        RunCase(verify, 0xE1C100B0u, true);
        RunCase(verify, 0xE5910000u, false);
        RunCase(verify, 0xE4910004u, false);
        RunCase(verify, 0xE5810000u, true);
        RunCase(verify, 0xE4810004u, true);
        RunCase(verify, 0xE8B10005u, false);
        RunCase(verify, 0xE8A10005u, true);
    }
    std::printf("arm_mmio_abort_test passed\n");
    return 0;
}
