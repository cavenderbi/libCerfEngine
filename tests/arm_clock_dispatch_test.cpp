#include "arm_ir_rig.h"

#include <cerfengine/cycle_deadline.h>
#include <cerfengine/host_request_channel.h>

int main() {
    ArmIrRig rig(false, {});
    auto& state = *rig.Cpu().State();
    auto& guest = rig.Engine().Get<cerfengine::GuestEngine>();
    uint32_t dispatches = 0u;
    rig.Host().OnClockDispatch([&] {
        ++dispatches;
        state.guest_cycle_deadline = cerfengine::CycleDeadline(state.guest_cycle_counter, 100u);
    });
    state.guest_cycle_deadline = 100u;
    TEST_CHECK(rig.Run({kRigBranchSelf}, {}) == cerfengine::GuestEngine::RunStatus::kRunning);
    TEST_CHECK(dispatches == 0u);
    uint32_t expected = 0u;
    for (uint32_t counter : {100u, 101u, 0xFFFFFFFEu, 0u, 1u}) {
        state.guest_cycle_counter = counter;
        state.guest_cycle_deadline = counter;
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
        TEST_CHECK(dispatches == ++expected);
        state.guest_cycle_counter = counter;
        state.guest_cycle_deadline = cerfengine::CycleDeadline(counter, 100u);
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
        TEST_CHECK(dispatches == expected);
        state.guest_cycle_counter = counter;
        state.guest_cycle_deadline = counter - 1u;
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
        TEST_CHECK(dispatches == ++expected);
    }
    auto& requests = rig.Engine().Get<cerfengine::HostRequestChannel>();
    requests.RegisterListener([&] { state.guest_cycle_deadline = state.guest_cycle_counter; });
    state.guest_cycle_deadline = cerfengine::CycleDeadline(state.guest_cycle_counter, 100u);
    requests.Request();
    TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
    TEST_CHECK(dispatches == ++expected);
    std::printf("arm_clock_dispatch_test passed\n");
}
