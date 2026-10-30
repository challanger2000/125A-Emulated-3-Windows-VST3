#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "../vst3-port/core_checkpoint_bundle.h"
int main() {
 using B=eiii::vst3::CoreCheckpointBundle;
 using M=eiii::vst3::CoreMidiSection;
 using E=eiii::vst3::CoreExecutionSection;
 std::vector<uint16_t> ram(0x10000), samples(0x400000);
 ram[100]=0xabcd; samples[500]=0x1234;
 M::State midi; midi.events={{0.125,{0x90,64,100}}};
 midi.queue={0x90,64,100};midi.next=1;midi.head=1;midi.bit=4;
 E::State cpu{0xabcde,0xfedcba9876543210ULL};
 std::vector<uint8_t> bytes;
 assert(B::encode(ram.data(),ram.size(),samples.data(),samples.size(),midi,cpu,bytes));
 std::vector<uint16_t> restoredRam,restoredSamples;
 M::State restoredMidi; E::State restoredCpu;
 assert(B::decode(bytes.data(),bytes.size(),restoredRam,restoredSamples,restoredMidi,restoredCpu));
 assert(restoredRam==ram&&restoredSamples==samples);
 assert(restoredMidi.queue==midi.queue&&restoredMidi.events.size()==1);
 assert(restoredCpu.pc==cpu.pc&&restoredCpu.completed_cycles==cpu.completed_cycles);
 bytes.back()^=0x80;
 assert(!B::decode(bytes.data(),bytes.size(),restoredRam,restoredSamples,restoredMidi,restoredCpu));
 assert(restoredRam==ram&&restoredCpu.pc==cpu.pc);
 bytes.back()^=0x80;
 assert(!B::decode(bytes.data(),bytes.size()-1,restoredRam,restoredSamples,restoredMidi,restoredCpu));
 assert(restoredSamples==samples&&restoredCpu.completed_cycles==cpu.completed_cycles);
 // Corrupt inner section-length metadata and repair the outer checksum so the
 // rejection is specifically due to the bundle's section bounds, not its CRC.
 auto malformed=bytes;
 constexpr size_t envelopeHeader=eiii::vst3::CoreStateEnvelope::header_size;
 const size_t sectionLengthOffset=envelopeHeader+4;
 // The first section claims to extend past the entire decoded bundle body.
 const uint32_t impossibleLength=UINT32_MAX;
 for(unsigned i=0;i<4;++i)
  malformed[sectionLengthOffset+i]=uint8_t(impossibleLength>>(i*8));
 const uint32_t repairedChecksum=eiii::vst3::CoreStateEnvelope::checksum(
     malformed.data()+envelopeHeader,malformed.size()-envelopeHeader);
 for(unsigned i=0;i<4;++i)
  malformed[12+i]=uint8_t(repairedChecksum>>(i*8));
 assert(!B::decode(malformed.data(),malformed.size(),restoredRam,
                   restoredSamples,restoredMidi,restoredCpu));
 assert(restoredRam==ram&&restoredSamples==samples);
 assert(restoredMidi.queue==midi.queue&&restoredCpu.pc==cpu.pc);
 // Also reject a non-overflowing but inconsistent second section length.
 malformed=bytes;
 const size_t secondLengthOffset=envelopeHeader+8;
 for(unsigned i=0;i<4;++i) malformed[secondLengthOffset+i]=0;
 const uint32_t repairedChecksum2=eiii::vst3::CoreStateEnvelope::checksum(
     malformed.data()+envelopeHeader,malformed.size()-envelopeHeader);
 for(unsigned i=0;i<4;++i)
  malformed[12+i]=uint8_t(repairedChecksum2>>(i*8));
 assert(!B::decode(malformed.data(),malformed.size(),restoredRam,
                   restoredSamples,restoredMidi,restoredCpu));
 assert(restoredRam==ram&&restoredSamples==samples);
 assert(restoredMidi.queue==midi.queue&&restoredCpu.pc==cpu.pc);
 // Forge a valid OUTER envelope with an invalid nested partial-state
 // checksum. Decoder must reject it without publishing partial results.
 malformed=bytes;
 const size_t nestedPayloadOffset=envelopeHeader+12;
 assert(nestedPayloadOffset+12<malformed.size());
 malformed[nestedPayloadOffset+12]^=0x01;
 const uint32_t outerChecksum=eiii::vst3::CoreStateEnvelope::checksum(
     malformed.data()+envelopeHeader,malformed.size()-envelopeHeader);
 for(unsigned i=0;i<4;++i)
  malformed[12+i]=uint8_t(outerChecksum>>(i*8));
 assert(!B::decode(malformed.data(),malformed.size(),restoredRam,
                   restoredSamples,restoredMidi,restoredCpu));
 assert(restoredRam==ram&&restoredSamples==samples);
 assert(restoredMidi.queue==midi.queue&&restoredMidi.events.size()==1);
 assert(restoredCpu.pc==cpu.pc&&restoredCpu.completed_cycles==cpu.completed_cycles);
}
