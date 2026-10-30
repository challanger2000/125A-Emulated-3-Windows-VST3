// Test only the deterministic firmware-facing panel macro plumbing.
// Actual OS 2.42 cursor and ADC semantics still require original-ROM QA.
#include "../../upstream/standalone/ui_macro.h"
#include "../../upstream/standalone/first_start_midi.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static void require(bool ok) {if(!ok)throw std::runtime_error("firmware MIDI default regression");}

int main()
{
    std::vector<std::string> events;
    std::string screen = "P00 Untitled Preset\nS00 Empty Song";
    ui_macro macro;
    macro.hold_s = 0.005;
    macro.gap_s = 0.005;
    macro.settle_s = 0.005;
    macro.panel = [&](std::string const& key, bool down) {
        events.push_back(std::string(down ? "+" : "-") + key);
    };
    macro.extra = macro.panel;
    macro.data_slider = [&](uint16_t adc) {
        events.push_back("ADC" + std::to_string(adc));
    };
    macro.lcd = [&] { return screen; };
    macro.wait("^P\\d\\d .*\\nS\\d\\d ", 1, "main");
    macro.extra_press("Master");
    macro.press("Keypad 8");
    macro.press("Keypad 7");
    macro.slider(0x7100);
    macro.press("Cursor Down");
    macro.press("Cursor Down");
    macro.slider(1);
    macro.extra_press("Enter");
    for (int i = 0; i < 1000 && !macro.done() && !macro.failed(); ++i)
        macro.tick(i * 0.01);
    require(macro.done() && !macro.failed());
    require(events.size() == 14);
    require(events[0] == "+Master" && events[1] == "-Master");
    require(events[6] == "ADC28928");
    require(events[11] == "ADC1");
    require(events[12] == "+Enter" && events[13] == "-Enter");

    ui_macro missing;
    missing.slider(42);
    missing.tick(0);
    require(missing.failed() && !missing.done());
    require(missing.error().find("DATA slider") != std::string::npos);
    // ADC selection must follow the firmware screen, not assume one calibrated value.
    ui_macro seek;
    seek.gap_s = 0.005;
    seek.settle_s = 0.005;
    std::string mode = "MIDI Mode: poly";
    unsigned probes=0;
    seek.data_slider=[&](uint16_t value) {
        ++probes;
        if (value >= 0x1000u && value <= 0x5000u)
            mode = "MIDI Mode: omni";
    };
    seek.lcd=[&] { return mode; };
    seek.seek_slider("MIDI Mode:\\s*omni", 5, "MIDI Mode Omni");
    for(int i=0; i<500 && !seek.done() && !seek.failed(); ++i)
        seek.tick(i*0.05);
    require(seek.done() && !seek.failed());
    require(probes > 1);

    ui_macro no_match;
    no_match.data_slider=[](uint16_t) {};
    no_match.lcd=[] { return std::string("MIDI Mode: poly"); };
    no_match.seek_slider("MIDI Mode:\\s*omni", 20, "MIDI Mode Omni");
    for(int i=0; i<500 && !no_match.done() && !no_match.failed(); ++i)
        no_match.tick(i*0.05);
    require(no_match.failed());
    require(no_match.error().find("MIDI Mode Omni") != std::string::npos);
    require(eiii::needs_first_start_midi(false,false,0));
    require(!eiii::needs_first_start_midi(true,false,0));
    require(!eiii::needs_first_start_midi(false,true,0)); // v1 recall / explicit routing
    require(!eiii::needs_first_start_midi(false,false,1));
    eiii::lcd_state lcd{};std::fill(std::begin(lcd.ddram),std::end(lcd.ddram),' ');
    std::memcpy(lcd.ddram,"MIDI GLOBALS:    on",18);
    std::memcpy(lcd.ddram+0x40,"Basic Channel:    14",20);
    std::memcpy(lcd.ddram+0x14,"MIDI Mode:      poly",20);
    std::memcpy(lcd.ddram+0x54,"MIDI Overflow:   off",20);
    const auto four=eiii::firmware_menu_lcd(lcd);
    require(four.size()==84&&four.substr(42,20)=="MIDI Mode:      poly");
    const std::regex channel_one("Basic Channel:\\s*1(\\s|$)");
    for(unsigned channel=10;channel<=16;++channel)
        require(!std::regex_search("Basic Channel: "+std::to_string(channel)+"\n",channel_one));
    require(std::regex_search("Basic Channel:     1\n",channel_one));

    ui_macro defaults;unsigned field=0,basic=14,mode_probes=0,basic_probes=0;
    bool globals=false,omni=false,entered=false,main_screen=false;
    defaults.panel=[&](const std::string& key,bool down) {
        if(!down)return;
        if(key=="Cursor Down")++field;
        if(key=="Enter")entered=true;
        if(key=="Master"&&entered)main_screen=true;
    };
    defaults.extra=defaults.panel;
    defaults.data_slider=[&](uint16_t value) {
        if(field==0)globals=value==0x7100;
        else if(field==1){++basic_probes;if(value==0)basic=1;}
        else if(field==2){++mode_probes;if(value>=0x1000&&value<0x5000)omni=true;}
    };
    defaults.lcd=[&] {
        if(main_screen)return std::string("P00 Untitled Preset\nS00 Empty Song\n");
        return std::string("MASTER SPECIAL MIDI GLOBALS: ")+(globals?"on":"off")+
            "\nBasic Channel: "+std::to_string(basic)+"\nMIDI Mode: "+(omni?"omni":"poly")+"\n";
    };
    eiii::append_first_start_midi(defaults,"^P[0-9][0-9] .*\nS[0-9][0-9] ");
    for(unsigned i=0;i<10000&&!defaults.done()&&!defaults.failed();++i)defaults.tick(i*.01);
    require(defaults.done()&&!defaults.failed()&&basic==1&&omni&&entered&&main_screen);
    require(basic_probes==1&&mode_probes>1);
    std::puts("EIII firmware panel macro: ADC/order, four-row LCD, exact channel 1, Omni, recall gates PASS");
}
