// A11: production ADC snapshot -> local and VST3-remote panel adapters.
// The real eiii::runtime::snapshot() is exercised without proprietary ROMs.
#include "../../upstream/panel/eiii_panel_adapter.h"
#include "../../upstream/panel/eiii_panel_model.h"
#include "../../upstream/runtime/eiii_runtime.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
#ifdef NDEBUG
#undef NDEBUG
#endif

static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static bool near(double a,double b){return std::fabs(a-b)<0.0001;}

int main() {
    using namespace eiii;
    const int data=panel_find_control("data_slider");
    const int sample=panel_find_control("sample_input");
    const int master=panel_find_control("master_volume");
    check(data>=0&&sample>=0&&master>=0,"all three physical sliders are present");
    check(panel_controls[data].code==3 && panel_controls[sample].code==0 &&
          panel_controls[master].code==5,"real ADC codes X3 X0 X5");

    runtime machine;
    machine.set_adc_input(3,0x5300);
    machine.set_adc_input(0,0x1800);
    machine.set_adc_input(5,0x7000);
    panel_snapshot snap{};
    machine.snapshot(snap);
    check(snap.adc_valid && snap.adc_inputs[3]==0x5300 &&
          snap.adc_inputs[0]==0x1800 && snap.adc_inputs[5]==0x7000,
          "the real worker snapshot contains ADC mux hardware values");

    bool accept=true;
    struct Command {int control;uint16_t value;};
    std::vector<Command> commands;
    auto slider=[&](int index,uint16_t adc){
        if(!accept)return false;
        commands.push_back({index,adc});
        return true;
    };
    auto button=[](int,bool){};
    panel_adapter first(button,slider);
    panel_adapter second(button,slider);
    // Before a valid runtime snapshot, normal power-on/default fallback.
    const double bootDefault=first.slider(data);
    check(!near(bootDefault,adc_to_slider(snap.adc_inputs[3])),
          "the test must distinguish wrong reboot default");
    panel_snapshot unavailable{};unavailable.adc_valid=false;
    first.publish_remote(unavailable);
    check(near(first.slider(data),bootDefault),
          "unavailable emulation snapshot must not invent ADC positions");

    first.publish_remote(snap);
    check(near(first.slider(data),adc_to_slider(0x5300)) &&
          near(first.slider(sample),adc_to_slider(0x1800)) &&
          near(first.slider(master),adc_to_slider(0x7000)),
          "first GUI follows hardware X3/X0/X5");
    // Two views of the same instance must converge on the same hardware.
    second.publish_remote(snap);
    check(near(second.slider(data),first.slider(data)) &&
          near(second.slider(master),first.slider(master)),
          "second GUI opening must not revert to power-on defaults");

    // Firmware automation writes X3 directly; only source hardware changed.
    machine.set_adc_input(3,0x2300);
    machine.set_adc_input(5,0x6100);
    machine.snapshot(snap);
    first.publish_remote(snap);
    second.publish_remote(snap);
    check(near(first.slider(data),adc_to_slider(0x2300)) &&
          near(second.slider(data),adc_to_slider(0x2300)) &&
          near(first.slider(master),adc_to_slider(0x6100)) &&
          commands.empty(),
          "firmware ADC changes update both views without echoing GUI commands");

    // Queue-full / rejected slider movement must not update a view.
    accept=false;
    const double before=first.slider(data);
    check(!first.set_slider(data,.89),"rejected remote slider must report failure");
    check(near(first.slider(data),before) && commands.empty(),
          "rejected input must not replace the confirmed slider position");
    accept=true;
    check(first.set_slider(data,.89),"valid GUI movement accepted");
    check(commands.size()==1 && commands.front().control==data &&
          commands.front().value==slider_to_adc(.89),"accepted command used correct ADC hardware channel");
    check(near(first.slider(data),.89),"accepted movement locally previewed");
    // Worker has not processed that message: published hardware overrides preview.
    first.publish_remote(snap);
    check(near(first.slider(data),adc_to_slider(0x2300)),
          "real snapshot, not provisional preview, is authoritative");
    // Process the accepted command on the emulated machine, then publish.
    machine.set_adc_input(3,commands.front().value);
    machine.snapshot(snap);
    first.publish_remote(snap);
    second.publish_remote(snap);
    check(near(first.slider(data),.89) && near(second.slider(data),.89),
          "both GUI instances follow newly confirmed hardware ADC");

    // Close + reopen editor (fresh adapter) must retain the actual ADC.
    panel_adapter reopened(button,slider);
    reopened.publish_remote(snap);
    check(near(reopened.slider(data),.89) &&
          near(reopened.slider(sample),adc_to_slider(0x1800)) &&
          near(reopened.slider(master),adc_to_slider(0x6100)),
          "reopen reconstructs all slider positions from runtime, not defaults");
    check(!reopened.set_slider(-1,.4) &&
          !reopened.set_slider(panel_find_control("contrast"),.4),
          "invalid and non-ADC controls cannot send slider messages");
    std::puts("PASS A11: real ADC snapshots, firmware moves, two views, queue rejection, reopen");
    return 0;
}
