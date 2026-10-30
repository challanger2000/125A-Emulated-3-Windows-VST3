// B02: real EIII motherboard peripheral-state capture and restore.
// Synthetic, ROM-free test; CPU/scheduler/SCSI/voice are deliberately separate.
#include "../../upstream/runtime/eiii_machine.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <limits>

int main() {
 using eiii::machine_t;
 auto first=std::make_unique<machine_t>();
 auto second=std::make_unique<machine_t>();
 std::fill(std::begin(first->lcd_ddram),std::end(first->lcd_ddram),0x20);
 std::fill(std::begin(second->lcd_ddram),std::end(second->lcd_ddram),0x20);
 first->dig_words[4]=0xa137;first->rf_latch=0x12345678;
 first->cv_words[5]=0x7ffc;
 first->w_start[1]=0x1234;first->w_loop[1]=0x3344;
 first->w_size[1]=0x5678;first->w_pending[1]=true;
 first->n_rf=419;first->n_ctrl=100;first->n_cv=88;
 first->irq_latch=0x5e;first->irq_state=true;
 auto& pit=first->pit[1];
 pit.clock_hz=1200000;pit.last_t=13579;pit.mode=3;pit.rw=3;
 pit.bcd=0;pit.reload=1000;pit.count=344;pit.loaded=true;
 pit.rem=99999;pit.load_delay=1;pit.out=false;pit.out_known=true;
 pit.gate=true;pit.latch=500;pit.latched=true;pit.msb_next=true;
 first->lcd_ddram[6]='B';first->lcd_cgram[2]=0xa5;
 first->lcd_ac=0x42;first->lcd_dir=-1;first->lcd_disp_shift=10;
 first->lcd_num_line=2;first->lcd_display_on=true;
 first->lcd_blink_on=true;first->lcd_busy_until=13600.5;
 first->scan_head=15;first->scan_tail=1;first->scan_count=2;
 first->scan_fifo[15]=0x17;first->scan_fifo[0]=0x32;
 first->extra_switches=0xc0;first->leds=0x5500;first->misc=0x80;
 first->fdc_status=0xa4;first->adc_ctrl=0x38;
 first->adc_latch[0]=-1002;first->adc_latch[1]=8191;
 first->adc_pot[0]=0x3333;first->adc_pot[3]=0x4444;
 auto const saved=first->capture_motherboard_recall();
 assert(machine_t::valid_motherboard_recall(saved));
 // Restore into an independently built motherboard; preserve its callbacks.
 int callbacks=0;
 second->pit[1].out_cb=[&](bool){++callbacks;};
 assert(second->restore_motherboard_recall(saved));
 assert(callbacks==0);
 assert(static_cast<bool>(second->pit[1].out_cb));
 assert(second->dig_words[4]==0xa137 && second->rf_latch==0x12345678);
 assert(second->cv_words[5]==0x7ffc);
 assert(second->w_start[1]==0x1234 && second->w_loop[1]==0x3344);
 assert(second->w_size[1]==0x5678 && second->w_pending[1]);
 assert(second->n_rf==419 && second->n_ctrl==100 && second->n_cv==88);
 assert(second->irq_latch==0x5e && second->irq_state);
 assert(second->pit[1].clock_hz==1200000);
 assert(second->pit[1].mode==3 && second->pit[1].count==344);
 assert(second->pit[1].rem==99999 && second->pit[1].latched);
 assert(second->lcd_ddram[6]=='B' && second->lcd_cgram[2]==0xa5);
 assert(second->lcd_ac==0x42 && second->lcd_num_line==2);
 assert(second->lcd_dir==-1 && second->lcd_disp_shift==10);
 assert(second->scan_count==2 && second->scan_head==15 && second->scan_tail==1);
 assert(second->scan_fifo[15]==0x17 && second->scan_fifo[0]==0x32);
 assert(second->fdc_status==0xa4 && second->leds==0x5500);
 assert(second->adc_latch[0]==-1002 && second->adc_latch[1]==8191);
 assert(second->adc_pot[0]==0x3333 && second->adc_pot[3]==0x4444);
 second->pit[1].out_cb(true);assert(callbacks==1);
 // Reject corrupted state before modifying any field; no callbacks invoked.
 auto invalid=saved;
 invalid.scan_count=17;
 assert(!machine_t::valid_motherboard_recall(invalid));
 assert(!second->restore_motherboard_recall(invalid));
 assert(second->scan_count==2 && second->rf_latch==0x12345678);
 invalid=saved;invalid.pit[1].last_t=std::numeric_limits<double>::infinity();
 assert(!second->restore_motherboard_recall(invalid));
 assert(second->pit[1].last_t==13579);
 invalid=saved;invalid.lcd_num_line=7;
 assert(!second->restore_motherboard_recall(invalid));
 assert(second->lcd_num_line==2);
 invalid=saved;invalid.scan_head=14; // invalid FIFO head/tail/count relation
 assert(!second->restore_motherboard_recall(invalid));
 assert(second->scan_head==15);
 invalid=saved;invalid.pit[1].rem=10'000'000ULL;
 assert(!second->restore_motherboard_recall(invalid));
 assert(second->pit[1].rem==99999 && callbacks==1);
 // Separate instances do not share peripheral state.
 first->lcd_ddram[6]='X';
 assert(second->lcd_ddram[6]=='B');
 std::puts("PASS B02: independent motherboard PIT/LCD/scanner/ADC/output capture/restore, callback preservation, all-or-nothing invalid state");
}
