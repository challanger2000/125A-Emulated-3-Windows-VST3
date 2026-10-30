// Real MAME WD1772 register/PLL regression over a synthetic EIII disk.
#include "emu.h"
#include "machine/wd_fdc.h"
#include "imagedev/floppy.h"
#include "../vst3-port/core_d0_section.h"
#include <filesystem>

int main()
{
	shim_scheduler sched;auto previous=g_shim_sched;g_shim_sched=&sched;
	int failures=0;
	auto check=[&](bool ok,char const *what){printf("%s %s\n",ok?"PASS":"FAIL",what);if(!ok)++failures;};
	auto path=std::filesystem::temp_directory_path()/"125a-eiii-d0-controller.img";
	std::vector<u8> image(819200);for(size_t i=0;i<image.size();++i)image[i]=u8((i/512)*37+i);
	{std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(image.data()),image.size());}
	{
		machine_config cfg;floppy_image_device disk(cfg);wd1772_device fdc(cfg,"fdc",nullptr,8'000'000);
		bool drq=false,irq=false;fdc.drq_wr_callback().set([&](int s){drq=s;});fdc.intrq_wr_callback().set([&](int s){irq=s;});
		fdc.set_disable_motor_control(true);disk.start();fdc.start();disk.reset();fdc.reset();fdc.set_floppy(&disk);fdc.dden_w(0);
		check(fdc.status_r()==4,"empty drive starts on track zero");
		std::string error;check(disk.load(eiii::shim::utf8_from_path(path),error),"mount synthetic 800 KiB floppy");disk.mon_w(0);
		auto run=[&](double seconds,std::vector<u8> *data=nullptr){
			auto end=sched.time()+attotime::from_double(seconds);size_t events=0;
			while(sched.next_expiry()<=end && events++<1000000){sched.fire_due(sched.next_expiry());if(data && drq)data->push_back(fdc.data_r());if(irq)break;}
			if(events>=1000000){
				printf("TIMER LOOP LIMIT at t=%.12f next=%.12f end=%.12f timerCount=%zu\\n",
				       sched.time().as_double(),sched.next_expiry().as_double(),end.as_double(),sched.m_timers.size());
				for(size_t i=0;i<sched.m_timers.size();++i){auto* timer=sched.m_timers[i];
				  if(timer->m_enabled && timer->m_expire<=sched.time())
				    printf("DUE TIMER %zu expire=%.12f period=%.12f\\n",i,timer->m_expire.as_double(),timer->m_period.as_double());
				}
			}
			if(!irq)sched.fire_due(end);
		};
		fdc.cmd_w(0xd0);run(.001);check(!irq && !(fdc.status_r()&1),"force interrupt D0 clears busy without asserting IRQ");
		for(int track:{0,79}) {
			fdc.data_w(track);fdc.cmd_w(0x18);run(2);check(irq && fdc.track_r()==track,"seek completes with IRQ and track register update");fdc.status_r();
			for(int side=0;side<2;++side)for(int sector:{1,10}) {
				disk.ss_w(side);fdc.sector_w(sector);fdc.cmd_w(0x88);std::vector<u8> data;run(2,&data);
				unsigned status=fdc.status_r();auto start=image.begin()+((track*2+side)*10+sector-1)*512;
				const bool same = data.size()==512 && std::equal(data.begin(),data.end(),start);
				if (irq || !same || (status&0x1c)) {
					const auto firstMismatch = (data.size()==512 && !same)
					    ? std::mismatch(data.begin(),data.end(),start).first-data.begin() : -1;
					printf("DETAIL track=%d side=%d sector=%d data=%zu status=%02X irq=%d drq=%d match=%d mismatch=%td sched=%.9f\n",
					       track,side,sector,data.size(),status,int(irq),int(drq),int(same),firstMismatch,sched.time().as_double());
				}
				if(!same || (status&0x1c)) {
					printf("DIAG drive ready=%d trk00=%d index=%d wdTrack=%u wdSector=%u nextIndex=%.12f\n",
					       disk.ready_r(),disk.trk00_r(),disk.idx_r(),fdc.track_r(),fdc.sector_r(),
					       disk.time_next_index().as_double());
					for(int retry=1;retry<=3;++retry) {
						fdc.cmd_w(0xd0);run(.01);fdc.status_r();
						fdc.sector_w(sector);fdc.cmd_w(0x8c);std::vector<u8> again;run(1,&again);
						const unsigned st=fdc.status_r();
						const bool matched=again.size()==512&&std::equal(again.begin(),again.end(),start);
						printf("DIAG retry=%d bytes=%zu status=%02X matched=%d at %.12f\n",
						       retry,again.size(),st,int(matched),sched.time().as_double());
						if(matched && !(st&0x1c))break;
					}
				}
				check(irq==false && same && !(status&0x1c),"sector bytes and CRC correct at both sides and disk boundaries; status read acknowledges IRQ");
			}
		}
		// Freeze during an *actual* WD1772 sector transfer, not at an idle
		// boundary. Re-run the suffix after restoring the scheduler, drive,
		// controller registers, CRC/live decoder and both PLL checkpoints.
		fdc.cmd_w(0xd0);run(.01);fdc.status_r();
		fdc.data_w(0);fdc.cmd_w(0x18);run(2);fdc.status_r();
		disk.ss_w(0);fdc.sector_w(1);fdc.cmd_w(0x88);
		std::vector<u8> prefix;
		for(size_t step=0;step<1000000 && prefix.size()<128 && !irq;++step){
			const auto next=sched.next_expiry();
			if(next.is_never())break;
			sched.fire_due(next);
			if(drq)prefix.push_back(fdc.data_r());
		}
		check(prefix.size()==128 && !irq,"D0 stopped at byte 128 of an in-flight sector");
		if(prefix.size()==128 && !irq){
			shim_scheduler::recall_state savedClock{};
			check(sched.capture_recall_state(savedClock),"D0 scheduler captured with pending live WD1772 timers");
			const auto savedDrive=disk.capture_recall_state();
			const auto savedFdc=fdc.capture_recall_state();
			check(disk.valid_recall_state(savedDrive) && fdc.valid_recall_state(savedFdc),
			      "D0 in-flight drive+controller+PLL checkpoint preflight succeeds");
			eiii::vst3::CoreD0Section::State checkpoint{};
			checkpoint.drive=savedDrive;checkpoint.controller=savedFdc;
			std::vector<uint8_t> blob;
			const bool encoded=eiii::vst3::CoreD0Section::encode(checkpoint,blob);
			eiii::vst3::CoreD0Section::State decoded{};
			check(encoded&&eiii::vst3::CoreD0Section::decode(blob.data(),blob.size(),decoded),
			      "D0 versioned checksum-protected head/controller/PLL snapshot roundtrip");
			if(encoded){
				auto damaged=blob;damaged.back()^=1;
				eiii::vst3::CoreD0Section::State rejected{};
				check(!eiii::vst3::CoreD0Section::decode(damaged.data(),damaged.size(),rejected),
				      "D0 damaged checkpoint checksum rejected before any device mutation");
			}
			std::vector<u8> suffixFirst;run(2,&suffixFirst);
			const auto statusFirst=fdc.status_r();
			const bool timerRestore=sched.restore_recall_state(savedClock);
			const bool diskRestore=disk.restore_recall_state(decoded.drive);
			const bool fdcRestore=fdc.restore_recall_state(decoded.controller);
			check(timerRestore&&diskRestore&&fdcRestore,
			      "D0 mid-sector scheduler, head, live CRC and PLL restore succeeds");
			if(timerRestore&&diskRestore&&fdcRestore){
				// MAME does not emit an IRQ edge on state restore; the test harness
				// must restore the two connected motherboard line latches itself.
				drq=savedFdc.controller.drq;irq=savedFdc.controller.intrq;
				std::vector<u8> suffixAgain;run(2,&suffixAgain);
				const auto statusAgain=fdc.status_r();
				std::vector<u8> combined=prefix;
				combined.insert(combined.end(),suffixAgain.begin(),suffixAgain.end());
				check(suffixFirst==suffixAgain && statusFirst==statusAgain &&
				      combined.size()==512 &&
				      std::equal(combined.begin(),combined.end(),image.begin()),
				      "D0 byte-for-byte deterministic resume from a mid-sector WD1772/PLL checkpoint");
			}
		}
		fdc.cmd_w(0xa8);run(2);check(irq && (fdc.status_r()&0x40) && !drq,"write sector is refused by write protection");
		check(!disk.load(eiii::shim::utf8_from_path(path)+".missing",error),"missing image rejected without replacing mounted disk");
		const auto malformed=path.parent_path()/"125a-d0-truncated.img";
		{std::ofstream file(malformed,std::ios::binary);file.write(reinterpret_cast<const char*>(image.data()),819199);}
		check(!disk.load(eiii::shim::utf8_from_path(malformed),error),"truncated 819199-byte IMG rejected");
		std::filesystem::remove(malformed);
		fdc.sector_w(1);fdc.cmd_w(0x88);std::vector<u8> data;run(2,&data);const auto finalStatus=fdc.status_r();
		if(data.size()!=512) printf("DETAIL replacement read data=%zu status=%02X irq=%d drq=%d sched=%.9f\n",
		                      data.size(),finalStatus,int(irq),int(drq),sched.time().as_double());
		if(data.size()!=512) {
			fdc.cmd_w(0xd0);run(.01);fdc.status_r();
			fdc.sector_w(1);fdc.cmd_w(0x8c);
			std::vector<u8> retry;run(2,&retry);const auto st=fdc.status_r();
			printf("DIAG replacement retry data=%zu status=%02X ready=%d\n",retry.size(),st,disk.ready_r());
		}
		check(data.size()==512,"failed replacement retains the mounted medium");
		// A failed write and a malformed replacement may never modify the source.
		std::vector<u8> original(image.size());
		{std::ifstream saved(path,std::ios::binary);saved.read(reinterpret_cast<char*>(original.data()),original.size());
		 check(saved.gcount()==std::streamsize(image.size()) && original==image,
		       "D0 original image unchanged after WRITE and failed replacement");}
		disk.eject();check(disk.ready_r()==1,"eject deasserts ready");
	}
	// Regress the exact 8th-revolution failure observed on MSVC: binary
	// rounding must never return the CURRENT index/flux time to the WD1772.
	// Use a separate scheduler so this exhaustive boundary check does not
	// alter the timing of the normal sector/seek test above.
	{
		shim_scheduler boundary;g_shim_sched=&boundary;
		machine_config cfg;floppy_image_device disk(cfg);disk.start();disk.reset();
		std::string error;
		check(disk.load(eiii::shim::utf8_from_path(path),error),"boundary fixture loaded");
		disk.mon_w(0);
		bool index_ok=true,flux_ok=true;
		for(unsigned rev=1;rev<=128;++rev) {
			boundary.fire_due(attotime::from_double(double(rev)*0.2));
			const auto deadline=disk.time_next_index();
			if(!(deadline>boundary.time()) ||
			   (disk.idx_r() && (deadline-boundary.time()).as_double()<0.19)) {
				index_ok=false;
				printf("BAD INDEX rev=%u index=%d t=%.17f next=%.17f\\n",
				       rev,disk.idx_r(),boundary.time().as_double(),deadline.as_double());
			}
			if(!(disk.get_next_transition(boundary.time())>boundary.time())) flux_ok=false;
		}
		check(index_ok,"D0 index deadline strictly in the future across 128 revolutions");
		check(flux_ok,"D0 flux transition strictly in the future across 128 revolutions");
		// Lossless mechanical snapshot at an arbitrary stopped CPU boundary.
		disk.ss_w(1);disk.dir_w(0);disk.stp_w(0);disk.stp_w(1);
		const auto savedDrive=disk.capture_recall_state();
		check(disk.valid_recall_state(savedDrive),"D0 drive/media identity matches snapshot");
		disk.ss_w(0);disk.mon_w(1);
		check(disk.restore_recall_state(savedDrive),"D0 drive/head/index/rotation snapshot restored silently");
		const auto restoredDrive=disk.capture_recall_state();
		check(restoredDrive.cylinder==savedDrive.cylinder &&
		      restoredDrive.side==savedDrive.side &&
		      restoredDrive.motor==savedDrive.motor &&
		      restoredDrive.index==savedDrive.index &&
		      restoredDrive.origin==savedDrive.origin &&
		      restoredDrive.media_hash==savedDrive.media_hash,
		      "D0 mechanism checkpoint matches including rotation origin");
		// A different valid image at the same path is not the same medium.
		auto altered=path.parent_path()/"125a-d0-other.img";
		{auto changed=image;changed[0]^=0x55;
		 std::ofstream out(altered,std::ios::binary);
		 out.write(reinterpret_cast<const char*>(changed.data()),std::streamsize(changed.size()));}
		check(disk.load(eiii::shim::utf8_from_path(altered),error),"different synthetic D0 mounted");
		check(!disk.valid_recall_state(savedDrive) &&
		      !disk.restore_recall_state(savedDrive),
		      "D0 restore refuses a different image fingerprint without mutation");
		std::filesystem::remove(altered);

		// HFE v1 acceptance is a separate container-format contract. A blank,
		// synthetically generated 80-track/2-side MFM image has zero flux by
		// design; this does NOT stand in for a genuine EIII HFE bank read.
		std::vector<u8> hfe(1024u+80u*49u*512u,0);
		const char signature[]="HXCPICFE";
		std::copy(signature,signature+8,hfe.begin());
		hfe[8]=0;hfe[9]=80;hfe[10]=2;hfe[11]=0;
		hfe[12]=250;hfe[13]=0; // 250-kbit/s MFM
		hfe[18]=1;hfe[19]=0; // track lookup table at byte 512
		for(unsigned track=0;track<80;++track){
			const unsigned p=512+4*track,off=2+49*track,n=25000;
			hfe[p]=u8(off);hfe[p+1]=u8(off>>8);
			hfe[p+2]=u8(n);hfe[p+3]=u8(n>>8);
		}
		auto hfePath=path.parent_path()/"125a-d0-blank.hfe";
		{std::ofstream out(hfePath,std::ios::binary);out.write(reinterpret_cast<const char*>(hfe.data()),std::streamsize(hfe.size()));}
		check(disk.load(eiii::shim::utf8_from_path(hfePath),error),"valid blank 80-track HFE v1 DD image accepted");
		check(disk.ready_r()==0 && disk.get_next_transition(boundary.time()).is_never(),
		      "blank HFE has no invented flux transitions");
		auto invalidHfePath=path.parent_path()/"125a-d0-invalid.hfe";
		hfe[12]=200;
		{std::ofstream out(invalidHfePath,std::ios::binary);out.write(reinterpret_cast<const char*>(hfe.data()),std::streamsize(hfe.size()));}
		check(!disk.load(eiii::shim::utf8_from_path(invalidHfePath),error),"wrong-bitrate HFE rejected");
		std::filesystem::remove(invalidHfePath);
		hfe[12]=250;hfe.resize(hfe.size()-512);
		{std::ofstream out(invalidHfePath,std::ios::binary);out.write(reinterpret_cast<const char*>(hfe.data()),std::streamsize(hfe.size()));}
		check(!disk.load(eiii::shim::utf8_from_path(invalidHfePath),error),"truncated HFE track data rejected");
		check(disk.ready_r()==0 && disk.get_next_transition(boundary.time()).is_never(),
		      "failed HFE replacement retains previous blank disk");
		std::filesystem::remove(invalidHfePath);
		std::filesystem::remove(hfePath);
	}
	g_shim_sched=&sched; // The boundary scheduler and all its devices are destroyed first.
	std::filesystem::remove(path);g_shim_sched=previous;
	return failures?1:0;
}
