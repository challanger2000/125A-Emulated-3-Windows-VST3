// B02: real HDD target sector/seek state and source-medium identity.
// Fixture is a temporary synthetic disk, no ROM/OS and no user media.
#include "emu.h"
#include "machine/nscsi_bus.h"
#include "bus/nscsi/hd.h"
#include "imagedev/harddriv.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

int main() {
    namespace fs=std::filesystem;
    fs::path imagePath=fs::temp_directory_path()/"125A_b02_hdd_recall_512k.img";
    {
        std::vector<char> data(512u*1024u,0);
        data[0]='E';data[1]='I';data[2]='I';data[3]='I';
        std::ofstream out(imagePath,std::ios::binary|std::ios::trunc);
        assert(bool(out));
        out.write(data.data(),static_cast<std::streamsize>(data.size()));
        assert(bool(out));
    }
    {
        machine_config config;
        nscsi_bus_device bus(config,"scsi",nullptr);
        nscsi_harddisk_device hd(config,"harddisk",nullptr);
        harddisk_image_device disk(config,"image",&hd);
        bus.set_external_device(0,static_cast<nscsi_device_interface*>(&hd));
        bus.resolve();hd.resolve();disk.resolve();
        bus.start();hd.start();disk.start();
        assert(disk.load(imagePath.string()));
        hd.reset();
        assert(disk.exists() && disk.length()==512u*1024u);
        nscsi_harddisk_device::disk_target_recall_state original{};
        assert(hd.capture_disk_target_recall(original));
        assert(original.media_present);
        assert(original.media_length==512u*1024u);
        assert(original.bytes_per_sector==512);

        auto synthetic=original;
        synthetic.sector[0]=0xaa;synthetic.sector[511]=0x61;
        synthetic.lba=12;synthetic.cur_lba=11;
        synthetic.remaining_blocks=3;
        synthetic.last_cylinder=8;
        synthetic.inquiry={0x10,0x20,0x30};
        assert(hd.valid_disk_target_recall(synthetic));
        int irqCallbacks=0;
        bus.out_bsy_callback().set([&](int){++irqCallbacks;});
        assert(hd.restore_disk_target_recall(synthetic));
        assert(irqCallbacks==0);
        nscsi_harddisk_device::disk_target_recall_state after{};
        assert(hd.capture_disk_target_recall(after));
        assert(after.sector[0]==0xaa && after.sector[511]==0x61);
        assert(after.lba==12 && after.cur_lba==11 && after.remaining_blocks==3);
        assert(after.last_cylinder==8 && after.inquiry.size()==3);

        auto reject=[&](nscsi_harddisk_device::disk_target_recall_state const &bad) {
            assert(!hd.valid_disk_target_recall(bad));
            assert(!hd.restore_disk_target_recall(bad));
            nscsi_harddisk_device::disk_target_recall_state still{};
            assert(hd.capture_disk_target_recall(still));
            assert(still.sector[0]==0xaa && still.lba==12);
            assert(still.cur_lba==11 && still.inquiry.size()==3);
            assert(irqCallbacks==0);
        };
        auto bad=synthetic;bad.media_fingerprint^=1;reject(bad);
        bad=synthetic;bad.media_length+=512;reject(bad);
        bad=synthetic;bad.model_name="other drive";reject(bad);
        bad=synthetic;bad.bytes_per_sector=1024;reject(bad);
        bad=synthetic;bad.remaining_blocks=-1;reject(bad);
        bad=synthetic;bad.interleave=0;reject(bad);
        bad=synthetic;bad.rpm=0;reject(bad);
        bad=synthetic;bad.seek_exponent=std::numeric_limits<double>::quiet_NaN();reject(bad);
        bad=synthetic;bad.scsi.queue_write=33;reject(bad);

        assert(hd.restore_disk_target_recall(original));
        assert(hd.capture_disk_target_recall(after));
        assert(after.lba==original.lba && after.cur_lba==original.cur_lba);
        assert(after.sector[0]==original.sector[0] && irqCallbacks==0);
        std::puts("PASS B02: synthetic mounted HDD sector/seek snapshot, media fingerprint, no bus edges and invalid-state rollback");
    }
    std::error_code error;
    fs::remove(imagePath,error);
}
