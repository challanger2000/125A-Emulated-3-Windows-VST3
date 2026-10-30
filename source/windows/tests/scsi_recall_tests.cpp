// B02: genuine bundled MAME NCR5380 + SCSI bus register-state roundtrip.
// ROM-free: this does NOT snapshot connected disk/CD target transfers.
#include "emu.h"
#include "machine/nscsi_bus.h"
#include "machine/ncr5380.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>

int main() {
    machine_config config;
    nscsi_bus_device bus(config,"scsi",nullptr);
    ncr5380_device ncr(config,"ncr",nullptr);
    bus.set_external_device(0,static_cast<nscsi_device_interface*>(&ncr));
    // Same lifecycle order as runtime::impl: wire interfaces, then start.
    bus.resolve();
    ncr.resolve();
    bus.start();
    ncr.start();
    ncr.reset();

    int busyEdges=0;
    bus.out_bsy_callback().set([&](int){++busyEdges;});
    ncr.write(0,0x4a); // genuine output data register
    ncr.write(2,0x20); // parity-check mode register
    // Bus refid 0 belongs to the NCR5380, not to a test-owned fake.
    bus.data_w(0,0x5a);
    bus.ctrl_wait(0,nscsi_device_interface::S_REQ,nscsi_device_interface::S_ALL);
    bus.ctrl_w(0,nscsi_device_interface::S_BSY,nscsi_device_interface::S_BSY);
    nscsi_bus_device::recall_state wires{};
    ncr5380_device::recall_state chip{};
    assert(bus.capture_recall_state(wires));
    assert(ncr.capture_recall_state(chip));
    assert(wires.device_count==1 && wires.ctrl==nscsi_device_interface::S_BSY);
    assert(wires.data==0x5a && wires.devices[0].wait_ctrl==nscsi_device_interface::S_REQ);
    assert(chip.odata==0x4a && chip.mode==0x20);

    // Mutate both machines' registers; then restore without bus edge events.
    ncr.write(0,0x11);
    ncr.write(2,0);
    bus.ctrl_w(0,0,nscsi_device_interface::S_BSY);
    bus.data_w(0,0);
    const int edgesBeforeRestore=busyEdges;
    assert(bus.restore_recall_state(wires));
    assert(ncr.restore_recall_state(chip));
    assert(busyEdges==edgesBeforeRestore);
    nscsi_bus_device::recall_state checkedBus{};
    ncr5380_device::recall_state checkedChip{};
    assert(bus.capture_recall_state(checkedBus));
    assert(ncr.capture_recall_state(checkedChip));
    assert(checkedBus.ctrl==wires.ctrl && checkedBus.data==wires.data);
    assert(checkedBus.devices[0].wait_ctrl==wires.devices[0].wait_ctrl);
    assert(checkedChip.odata==chip.odata && checkedChip.mode==chip.mode);
    assert(checkedChip.phase==chip.phase && checkedChip.scsi_ctrl==chip.scsi_ctrl);

    auto badBus=wires;
    badBus.device_count=2;
    assert(!bus.restore_recall_state(badBus));
    badBus=wires;badBus.devices[0].tag="foreign-controller";
    assert(!bus.restore_recall_state(badBus));
    badBus=wires;badBus.devices[0].ctrl=0x80000000u;
    assert(!bus.restore_recall_state(badBus));
    badBus=wires;badBus.data=0; // conflicts with participant OR
    assert(!bus.restore_recall_state(badBus));
    badBus=wires;badBus.devices[0].data=0x100u; 
    assert(!bus.restore_recall_state(badBus));
    auto badChip=chip;
    badChip.phase=0xffffffffu;
    assert(!ncr.restore_recall_state(badChip));
    badChip=chip;badChip.scsi_ctrl=0x80000000u;
    assert(!ncr.restore_recall_state(badChip));
    assert(bus.ctrl_r()==wires.ctrl && bus.data_r()==wires.data);
    assert(ncr.capture_recall_state(checkedChip));
    assert(checkedChip.odata==chip.odata && checkedChip.mode==chip.mode);
    assert(busyEdges==edgesBeforeRestore);
    std::puts("PASS B02: actual NCR5380 SCSI/DMA registers, resolved bus participants, quiet restore and corrupt input rejection");
}
