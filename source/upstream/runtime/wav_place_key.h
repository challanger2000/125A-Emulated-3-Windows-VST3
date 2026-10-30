// Decode the actual firmware PLACE SAMPLE key field from LCD row two.
// The row carries different note fields for Original / Low / High Key.
// Pure parser: can be tested without proprietary Emulator III firmware.
#pragma once
#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <utility>
#include <regex>
#include <string>
#include <string_view>

namespace eiii::wav {
enum class PlaceKeyStage { original, low, high };

inline bool parse_place_key(std::string_view display, PlaceKeyStage stage,
                            unsigned& midi_note) {
    // After moving DATA the firmware replaces the prompt with the mapping
    // status ("No Primary Sample", etc.). That status must not invalidate
    // the note currently shown on the PLACE SAMPLE second LCD row.
    // The caller confirms the prompt BEFORE moving DATA; we validate the
    // stage by the number and structure of displayed note fields.
    if(display.find("PLACE SAMPLE")==std::string_view::npos)return false;
    const auto first=display.find('\n');
    if(first==std::string_view::npos)return false;
    const auto second=display.find('\n',first+1);
    if(second==std::string_view::npos)return false;
    const std::string row(display.substr(first+1,second-first-1));
    // The firmware puts the note fields after "pri"/"sec" on LCD line 2.
    const auto p=row.find("pri ");
    const auto s=row.find("sec ");
    const size_t start=p!=std::string::npos?p+4:(s!=std::string::npos?s+4:std::string::npos);
    if(start==std::string::npos)return false;
    static const std::regex note(R"(\b([A-G])([#b]?)(-?[0-9]{1,2})\b)");
    const auto count=stage==PlaceKeyStage::original?0u:
                     stage==PlaceKeyStage::low?1u:2u;
    std::sregex_iterator it(row.begin()+std::ptrdiff_t(start),row.end(),note),end;
    std::sregex_iterator selected=end;
    unsigned found=0;
    for(;it!=end;++it,++found)if(found==count)selected=it;
    // Requiring exactly 1 / 2 / 3 note fields prevents accidentally
    // reading a prior stage's key when its prompt has disappeared.
    if(found!=count+1 || selected==end)return false;
    if((stage==PlaceKeyStage::high)!=(row.find(" to ",start)!=std::string::npos))
        return false;
    const auto& m=*selected;
    int semi=0;
    switch(m[1].str()[0]){
        case 'D':semi=2;break;case 'E':semi=4;break;case 'F':semi=5;break;
        case 'G':semi=7;break;case 'A':semi=9;break;case 'B':semi=11;break;
        default:break;
    }
    if(m[2]=="#")++semi;
    if(m[2]=="b")--semi;
    // v16 firmware EDIT ASSIGNMENT measured MIDI 48=C2, 60=C3,
    // 72=C4, 84=C5. EIII calls MIDI middle C "C3".
    const int value=(std::stoi(m[3].str())+2)*12+semi;
    if(value<0 || value>127)return false;
    midi_note=unsigned(value);
    return true;
}

struct KeySeekResult { uint16_t adc=0; unsigned note=0, probes=0; };

// The direction (+ADC -> +pitch) comes from the firmware ADC/LCD trace.
// Search from a VALID displayed note, never calibrate by probing the
// physical endpoints: ADC zero produced "A"+NUL during Low Key in v13.
// Each successful candidate is checked against the actual firmware LCD.
template<class Probe>
bool seek_key_adc(uint16_t initial_adc,unsigned initial_note,unsigned target,
                  Probe&& sample,KeySeekResult& result,std::string& error) {
    result={initial_adc,initial_note,0}; error.clear();
    if(target>127 || initial_note>127 || initial_adc>0x7100) {
        error="invalid key or DATA slider starting value"; return false;
    }
    if(target==initial_note)return true;
    const int direction=target>initial_note?1:-1;
    int value=int(initial_adc);
    unsigned note=initial_note;
    int step=256;
    for(unsigned attempt=0;attempt<48;++attempt) {
        int next=std::clamp(value+direction*step,1,0x7100);
        if(next==value) { error="target is outside the measured DATA slider range"; return false; }
        unsigned candidate=0;
        ++result.probes;
        if(!sample(uint16_t(next),candidate)) {
            // An invalid LCD is never an admissible MIDI note or a
            // proved channel boundary. Retreat to the last valid note.
            if(step<=1) { error="no valid firmware note before DATA slider boundary"; return false; }
            step=std::max(1,step/2);
            continue;
        }
        if(candidate>127 || (direction>0?candidate<note:candidate>note)) {
            error="firmware DATA slider response is nonmonotonic"; return false;
        }
        if(candidate==target) { result.adc=uint16_t(next);result.note=candidate;return true; }
        if(direction>0?candidate>target:candidate<target) {
            // Overshoot brackets the target with TWO verified valid notes.
            int a=value,b=next;
            unsigned note_a=note,note_b=candidate;
            while(std::abs(a-b)>1 && result.probes<64) {
                const int mid=a+(b-a)/2;
                unsigned measured=0;
                ++result.probes;
                if(!sample(uint16_t(mid),measured) || measured>127) {
                    error="invalid firmware key inside observed note bracket";return false;
                }
                if(measured==target) {result.adc=uint16_t(mid);result.note=measured;return true;}
                if(direction>0?(measured<note_a || measured>note_b):
                               (measured>note_a || measured<note_b)) {
                    error="firmware slider note ordering changed inside bracket";return false;
                }
                if(direction>0?measured<target:measured>target) {a=mid;note_a=measured;}
                else {b=mid;note_b=measured;}
            }
            error="target MIDI key does not occur in validated firmware bracket";
            return false;
        }
        value=next; note=candidate;
        result.adc=uint16_t(value); result.note=note;
        step=std::min(step*2,0x2000);
    }
    error="key search exhausted bounded firmware probes";
    return false;
}


// The original EIII PLACE SAMPLE dialog begins with a firmware DEFAULT key
// (G2 in the Windows v14 trace), even when the DATA pot is at ADC 0x0001.
// This is NOT a calibrated (ADC,key) pair: at 0x0101...0x0002 the
// same trace shows "A"+NUL, not a musical note. Do not seed a search from
// the unobserved default. Probe safe INTERIOR ADC positions first:
// 0x3f90->A#3 (MIDI 70), 0x4154->C4 (MIDI 72) were observed
// in the original-key dialog (EIII naming, v16). All other stages must verify BOTH positions at runtime.
template<class Probe>
bool seek_key_measured(unsigned target,Probe&& sample,KeySeekResult& result,
                       std::string& error) {
    constexpr uint16_t lower_anchor=0x3f90,upper_anchor=0x4154;
    unsigned lower_note=0,upper_note=0;
    if(!sample(lower_anchor,lower_note)) {
        error="firmware did not show a valid note at safe lower calibration ADC 0x3f90";
        return false;
    }
    if(!sample(upper_anchor,upper_note)) {
        error="firmware did not show a valid note at safe upper calibration ADC 0x4154";
        return false;
    }
    if(lower_note>upper_note) {
        error="firmware key ordering at calibration anchors is inconsistent";
        return false;
    }
    // The pair at upper_anchor has now been observed DURING THIS STAGE,
    // so neither a default LCD note nor the previous stage is trusted.
    return seek_key_adc(upper_anchor,upper_note,target,
                        std::forward<Probe>(sample),result,error);
}

} // namespace eiii::wav
