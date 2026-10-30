// B02: ROM-free production CD target transaction and media guard regression.
#include "emu.h"
#include "machine/nscsi_bus.h"
#include "bus/nscsi/cd.h"
#include "imagedev/cdromimg.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
int main() {
 namespace fs=std::filesystem;
 auto path=fs::temp_directory_path()/"125a_b02_target_iso_test.iso";
 auto large=fs::temp_directory_path()/"125a_b02_target_large_iso_test.iso";
 std::vector<char> bytes(128*1024,0x42);
 { std::ofstream out(path,std::ios::binary|std::ios::trunc);assert(bool(out));out.write(bytes.data(),bytes.size());assert(bool(out)); }
 {
  machine_config config;
  nscsi_bus_device bus(config,"scsi",nullptr);
  nscsi_cdrom_device cd(config,"cdrom",nullptr);
  cdrom_image_device image(config,"image",&cd);
  bus.set_external_device(0,static_cast<nscsi_device_interface*>(&cd));
  bus.resolve();cd.resolve();image.resolve();
  bus.start();cd.start();image.start();
  assert(image.load(path.string()));cd.reset();
  nscsi_cdrom_device::cd_target_recall_state original{};
  assert(cd.capture_cd_target_recall(original));
  assert(original.media_present && original.media_length==bytes.size());
  assert(original.transfer.size()==64*1024);
  auto updated=original;
  updated.sector[17]=0xa5;updated.mode[3]=0x67;
  updated.transfer[10]=0xb3;updated.transfer_position=0;
  updated.lba=1;updated.cached_sector=1;
  assert(cd.valid_cd_target_recall(updated));
  int callbacks=0;
  bus.out_bsy_callback().set([&](int){++callbacks;});
  assert(cd.restore_cd_target_recall(updated));
  nscsi_cdrom_device::cd_target_recall_state actual{};
  assert(cd.capture_cd_target_recall(actual));
  assert(actual.sector[17]==0xa5 && actual.mode[3]==0x67 && actual.transfer[10]==0xb3);
  assert(actual.transfer_position==0 && actual.lba==1 && callbacks==0);
  auto reject=[&](nscsi_cdrom_device::cd_target_recall_state const &bad){
    assert(!cd.valid_cd_target_recall(bad));
    assert(!cd.restore_cd_target_recall(bad));
    nscsi_cdrom_device::cd_target_recall_state now{};
    assert(cd.capture_cd_target_recall(now));
    assert(now.sector[17]==0xa5 && now.transfer_position==0 && callbacks==0);
  };
  auto bad=updated;bad.media_fingerprint^=1;reject(bad);
  bad=updated;bad.media_length++;reject(bad);
  bad=updated;bad.media_sequence=-1;reject(bad);
  bad=updated;bad.cached_media_sequence=uint32_t(bad.media_sequence)+1;reject(bad);
  bad=updated;bad.block_size=300;reject(bad);
  bad=updated;bad.mode_size=255; // valid 256-byte mode payload
  assert(cd.valid_cd_target_recall(bad));
  bad=updated;bad.transfer_position=1;reject(bad);
  bad=updated;bad.transfer_position=65537;reject(bad);
  bad=updated;bad.scsi.buffer_id=4;reject(bad);
  bad=updated;bad.cached_sector=999999;reject(bad);
  bad=updated;bad.transfer.pop_back();reject(bad);
  bad=updated;bad.scsi.queue_write=33;reject(bad);
  assert(cd.restore_cd_target_recall(original));
  // A later mount has a different generation even when the ISO is identical.
  // Save both generation values: before the next SCSI command, the cached
  // device generation can legitimately lag behind the image generation.
  {std::vector<char> data(512*1024,0x23);std::ofstream out(large,std::ios::binary);out.write(data.data(),data.size());assert(bool(out));}
  assert(image.load(large.string()));
  auto oldCache=updated;oldCache.media_length=image.length();oldCache.media_fingerprint=image.fingerprint();
  oldCache.media_sequence=image.sequence_counter();oldCache.cached_media_sequence=uint32_t(oldCache.media_sequence);
  oldCache.cached_sector=200;assert(cd.restore_cd_target_recall(oldCache));
  // The larger disc's cached sector is outside the new smaller disc, but
  // remains a valid stale cache until the pending unit-attention is handled.
  image.unload();assert(image.load(path.string()));
  nscsi_cdrom_device::cd_target_recall_state pending{};
  assert(cd.capture_cd_target_recall(pending));
  assert(pending.media_sequence>int(pending.cached_media_sequence) && pending.cached_sector==200);
  assert(cd.restore_cd_target_recall(original));
  assert(image.sequence_counter()==original.media_sequence);
  assert(cd.restore_cd_target_recall(pending));
  assert(cd.capture_cd_target_recall(actual));
  assert(actual.media_sequence==pending.media_sequence &&
         actual.cached_media_sequence==pending.cached_media_sequence);
  image.unload();
  assert(!cd.restore_cd_target_recall(original));
  std::puts("PASS B02: CD target snapshot, no callback, invalid cursor/medium rejects");
 }
 std::error_code ec;fs::remove(path,ec);fs::remove(large,ec);
}
