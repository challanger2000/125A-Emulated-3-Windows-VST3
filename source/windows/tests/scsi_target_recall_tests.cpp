// B02 — real base SCSI target command/queue state (ROM-free).
// This does NOT cover the concrete HDD/CD sector and pending media state.
#include "emu.h"
#include "machine/nscsi_bus.h"
#include "machine/nscsi_hle.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

namespace {
const device_type_impl targetKind{"125a-b02-target","125A B02 Target Probe"};
class TargetProbe final : public nscsi_full_device {
public:
    TargetProbe(machine_config const &cfg, const char* tag)
       : nscsi_full_device(cfg,targetKind,tag,nullptr,0) {}
};
}

int main() {
    machine_config cfg;
    nscsi_bus_device bus(cfg,"scsi",nullptr);
    TargetProbe target(cfg,"probe0"), other(cfg,"probe1");
    bus.set_external_device(0,static_cast<nscsi_device_interface*>(&target));
    bus.set_external_device(1,static_cast<nscsi_device_interface*>(&other));
    bus.resolve();target.resolve();other.resolve();
    bus.start();target.start();other.start();
    target.reset();other.reset();

    nscsi_full_device::target_recall_state base{};
    nscsi_full_device::target_recall_state sibling{};
    assert(target.capture_target_recall(base));
    assert(other.capture_target_recall(sibling));
    int callbacks=0;
    bus.out_bsy_callback().set([&](int) { ++callbacks; });

    auto active=base;
    active.command[0]=0x28; // SCSI READ(10)
    active.command[1]=0x99;
    active.command_size=10;
    active.sense[0]=0x70;
    active.identify=0x80;
    active.state=0x105; // valid partial-ACK state
    active.substate=0;
    active.initiator_id=7;
    active.buffer_id=0;
    active.buffer_size=512;
    active.buffer_pos=57;
    active.queue_read=1;
    active.queue_write=3;
    active.actions[1]={4,0,512}; // BC_DATA_IN
    active.actions[2]={1,0,0}; // BC_STATUS
    assert(nscsi_full_device::valid_target_recall(active));
    assert(target.restore_target_recall(active));
    nscsi_full_device::target_recall_state captured{};
    assert(target.capture_target_recall(captured));
    assert(captured.command[0]==0x28 && captured.command[1]==0x99);
    assert(captured.command_size==10 && captured.buffer_pos==57);
    assert(captured.queue_read==1 && captured.queue_write==3);
    assert(captured.actions[1].action==4 && captured.actions[1].param2==512);
    assert(captured.state==0x105 && captured.initiator_id==7);
    assert(callbacks==0);

    auto reject=[&](nscsi_full_device::target_recall_state const &bad) {
        assert(!nscsi_full_device::valid_target_recall(bad));
        assert(!target.restore_target_recall(bad));
        nscsi_full_device::target_recall_state still{};
        assert(target.capture_target_recall(still));
        assert(still.command[1]==0x99 && still.buffer_pos==57);
        assert(still.queue_write==3 && still.actions[1].param2==512);
        assert(callbacks==0);
    };
    auto bad=active;bad.command_size=4097;reject(bad);
    bad=active;bad.queue_read=4;reject(bad);
    bad=active;bad.queue_write=33;reject(bad);
    bad=active;bad.buffer_pos=513;reject(bad);
    bad=active;bad.initiator_id=16;reject(bad);
    bad=active;bad.state=0x505;reject(bad);
    bad=active;bad.actions[1].action=55;reject(bad);
    bad=active;bad.actions[1].param2=-1;reject(bad);

    // Restoring one target must not affect the other target on the bus.
    nscsi_full_device::target_recall_state unchanged{};
    assert(other.capture_target_recall(unchanged));
    assert(unchanged.command_size==sibling.command_size);
    assert(unchanged.queue_read==sibling.queue_read);
    assert(unchanged.queue_write==sibling.queue_write);
    assert(target.restore_target_recall(base));
    assert(target.capture_target_recall(captured));
    assert(captured.command_size==base.command_size);
    assert(captured.queue_read==base.queue_read);
    assert(callbacks==0);
    std::puts("PASS B02: SCSI target command/sense, transfer cursor, phase and control queue quiet restore and validation");
}
