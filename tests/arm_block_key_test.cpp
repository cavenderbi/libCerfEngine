#include "arm_ir_rig.h"

#include "arm/arm_translation_cache.h"

namespace {

constexpr uint32_t kSctlrMmuOn = 0x00000001u;
constexpr uint32_t kSvcMode    = 0x000000D3u;
constexpr uint32_t kOtherAsid  = 5u;

void TestBlockAfterAsidChange(bool global, const char* name) {
    ArmIrRig rig(true, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().SetPagesGlobal(global);
    const std::vector<uint32_t> program = {kRigBranchSelf};
    rig.Run(program, cerfengine::ArmCpuState{}, kSctlrMmuOn, kSvcMode);
    const uint32_t before = rig.Host().BlocksCompiled();

    rig.Engine().Get<cerfengine::ArmMmu>().State()->contextidr = kOtherAsid;
    rig.Engine().Get<cerfengine::ArmTranslationCache>().ContextSwitchFlush();
    rig.Engine().Get<cerfengine::GuestEngine>().Run();
    const uint32_t recompiled = rig.Host().BlocksCompiled() - before;

    std::printf("%s: %u block(s) compiled after the ASID change\n", name, recompiled);
    TEST_CHECK(rig.State().gprs[15] == 0u);
    TEST_CHECK(recompiled == (global ? 0u : 1u));
}

void TestDataAfterAsidChange(bool global) {
    ArmIrRig rig(false, {});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().SetPagesGlobal(global);
    const uint32_t old_words[] = {0x11111111u, 0u};
    const uint32_t new_words[] = {0x22222222u, 0u};
    rig.Host().LoadWords(0x8000u, old_words, 2u);
    rig.Host().LoadWords(0x9000u, new_words, 2u);
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = 0x8000u;
    initial.gprs[3] = 0x33333333u;
    rig.Run({0xE5912000u, 0xE5813004u, kRigBranchSelf}, initial, kSctlrMmuOn, kSvcMode);
    TEST_CHECK(rig.State().gprs[2] == old_words[0]);
    rig.System().RemapPage(0x8000u, 0x9000u);
    rig.Engine().Get<cerfengine::ArmMmu>().State()->contextidr = kOtherAsid;
    rig.Engine().Get<cerfengine::ArmTranslationCache>().ContextSwitchFlush();
    rig.Cpu().State()->gprs[15] = 0u;
    rig.Cpu().State()->gprs[3] = 0x44444444u;
    rig.Engine().Get<cerfengine::GuestEngine>().Run();
    TEST_CHECK(rig.State().gprs[2] == (global ? old_words[0] : new_words[0]));
    const auto& ram = rig.Host().Ram();
    TEST_CHECK(cerfengine::le::U32(ram.data() + 0x8004u) ==
               (global ? 0x44444444u : 0x33333333u));
    TEST_CHECK(cerfengine::le::U32(ram.data() + 0x9004u) == (global ? 0u : 0x44444444u));
}

void TestMmioAfterAsidChange(bool global) {
    ArmIrRig rig(false, {});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().SetPagesGlobal(global);
    rig.System().RemapPage(0x8000u, kRigMmioPa);
    rig.Host().EnableMmioLog();
    const uint32_t words[] = {0x22222222u, 0u};
    rig.Host().LoadWords(0x9000u, words, 2u);
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = 0x8000u;
    initial.gprs[3] = 0x33333333u;
    rig.Run({0xE5912000u, 0xE5813004u, kRigBranchSelf}, initial, kSctlrMmuOn, kSvcMode);
    TEST_CHECK(rig.Host().MmioLog().size() == 2u);
    rig.System().RemapPage(0x8000u, 0x9000u);
    rig.Engine().Get<cerfengine::ArmMmu>().State()->contextidr = kOtherAsid;
    rig.Engine().Get<cerfengine::ArmTranslationCache>().ContextSwitchFlush();
    rig.Cpu().State()->gprs[15] = 0u;
    rig.Engine().Get<cerfengine::GuestEngine>().Run();
    TEST_CHECK(rig.Host().MmioLog().size() == (global ? 4u : 2u));
    const auto& log = rig.Host().MmioLog();
    for (size_t i = 0u; i < log.size(); i += 2u) {
        TEST_CHECK(log[i].pa == kRigMmioPa && !log[i].write && log[i].width == 4u);
        TEST_CHECK(log[i + 1u].pa == kRigMmioPa + 4u && log[i + 1u].write &&
                   log[i + 1u].width == 4u && log[i + 1u].value == 0x33333333u);
    }
    TEST_CHECK(cerfengine::le::U32(rig.Host().Ram().data() + 0x9004u) ==
               (global ? 0u : 0x33333333u));
    if (!global) TEST_CHECK(rig.State().gprs[2] == words[0]);
}

}

int main() {
    for (const bool global : {true, false}) {
        TestBlockAfterAsidChange(global, global ? "global" : "non-global");
        TestDataAfterAsidChange(global);
        TestMmioAfterAsidChange(global);
    }
    std::printf("arm_block_key_test passed\n");
    return 0;
}
