// Host operator defaults; original firmware handles every edit and disk write.
#pragma once
#include "ui_macro.h"
#include "../runtime/eiii_runtime.h"
namespace eiii {
inline bool needs_first_start_midi(bool project_loaded, bool midi_settings_provided,
                                  unsigned engine_starts) noexcept {
    return !project_loaded && !midi_settings_provided && engine_starts==0;
}
// HD44780 physical 2x40 DDRAM folded into the actual EIII 4x20 display.
// lcd_line_text() is a physical-line helper and must not receive rows 2 or 3.
inline std::string firmware_menu_lcd(lcd_state const& lcd) {
    std::string text;
    for(unsigned base:{0u,0x40u,0x14u,0x54u}) {
        for(unsigned col=0;col<20;++col) {
            const auto c=lcd.ddram[base+col];
            text+=c>=32&&c<127?char(c):' ';
        }
        text+='\n';
    }
    return text;
}
inline void append_first_start_midi(ui_macro& menu,const std::string& ready) {
    menu.extra_press("Master");
    menu.press("Keypad 8");
    menu.wait("SPECIAL",10,"Master Special menu");
    menu.press("Keypad 7");
    menu.wait("MIDI GLOBALS",10,"firmware MIDI globals");
    menu.slider(0x7100);
    menu.wait("MIDI GLOBALS:\\s*on",10,"firmware MIDI globals enabled");
    menu.press("Cursor Down");
    // Full numeric boundary: channels 10..16 must never satisfy channel 1.
    menu.seek_slider("Basic Channel:\\s*1(\\s|$)",12,"Basic Channel 1");
    menu.press("Cursor Down");
    menu.seek_slider("MIDI Mode:\\s*omni",12,"MIDI Mode Omni");
    menu.extra_press("Enter"); // original firmware persists globals to D1 overlay
    menu.extra_press("Master");
    menu.wait(ready,30,"main screen after MIDI globals setup");
}
} // namespace eiii
