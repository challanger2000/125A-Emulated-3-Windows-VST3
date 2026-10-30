// Firmware-driven automatic bank loading on a dedicated non-audio operator thread.
// Reuses the upstream panel macro: all actions are real scanner / extra-switch inputs.
// No direct writes to emulated RAM, sample memory, or voice state.
#include "runtime/eiii_engine.h"
#include "panel/eiii_panel_model.h"
#include "standalone/ui_macro.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <thread>

namespace {
void note(char const* text) {
    if (const char* path = std::getenv("EIII_DIAG_FILE"))
        if (FILE* file = std::fopen(path, "a")) {
            std::fprintf(file, "AUTOLOAD: %s\n", text);
            std::fclose(file);
        }
}
} // anonymous namespace

void eiii_autoload_bank(eiii::engine &eng, std::atomic<bool> &cancel,
                        int bank, int drive, int preset)
{
    ui_macro macro;
    eiii::panel_snapshot snap{};
    auto send_panel = [&](std::string const &internal, bool down) {
        for (size_t i = 0; i < eiii::panel_control_count; ++i) {
            auto const &control = eiii::panel_controls[i];
            if (internal != control.internal) continue;
            if (control.route == eiii::panel_route::scan)
                eng.post({eiii::panel_input::SCAN, control.code, uint8_t(down), 0});
            else if (control.route == eiii::panel_route::xsw)
                eng.post({eiii::panel_input::XSW, control.code, uint8_t(down), 0});
            return;
        }
    };
    macro.panel = send_panel;
    macro.extra = send_panel;
    macro.lcd = [&]() {
        auto row = [&](int offset) {
            std::string out;
            for (int i = 0; i < 16; ++i) {
                const unsigned char c = snap.lcd.ddram[offset+i];
                out += c >= 32 && c <= 126 ? char(c) : ' ';
            }
            return out;
        };
        return row(0) + "\n" + row(0x40);
    };
    macro.m_log = [](double, std::string const &what) { note(what.c_str()); };
    std::string const ready = "^P\\d\\d .*\\nS\\d\\d ";
    macro.wait(ready, 60, "main screen");
    auto key = [&](int k) { macro.press("Keypad " + std::to_string(k)); };
    macro.press("Load Bank");
    macro.wait("LOAD BANK from", 10, "LOAD BANK");
    macro.extra_press("Enter");
    macro.wait("Select a Drive", 10, "drive selection");
    key(drive);
    macro.wait("\\nD" + std::to_string(drive) + " ", 10, "drive");
    macro.extra_press("Enter");
    macro.wait("Select a Bank", 30, "bank list");
    key(bank / 10 % 10);
    key(bank % 10);
    char bb[16];
    std::snprintf(bb, sizeof(bb), "B%02d ", bank);
    macro.wait(bb, 10, "bank");
    macro.extra_press("Enter");
    macro.wait("Loading Bank", 10, "loading");
    macro.wait(ready, 120, "main screen with the bank");
    if (preset >= 0) {
        key(preset / 10 % 10);
        key(preset % 10);
        char pp[16];
        std::snprintf(pp, sizeof(pp), "^P%02d ", preset);
        macro.wait(pp, 10, "preset");
    }
    note("Starting front-panel bank loader");
    auto start = std::chrono::steady_clock::now();
    while (!cancel.load(std::memory_order_relaxed) && !macro.done() && !macro.failed()) {
        if (eng.snapshot(snap) && snap.running) macro.tick(snap.emu_seconds);
        if (std::chrono::steady_clock::now() - start > std::chrono::minutes(5)) {
            note("ERROR: operator timed out (wall clock)");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (macro.failed()) {
        note(("ERROR: " + macro.error()).c_str());
    } else if (macro.done()) {
        note("Completed front-panel bank load and preset selection");
    } else {
        note("Cancelled");
    }
}
