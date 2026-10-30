// A13: Windows native UTF-16 -> internal strict UTF-8 -> actual file I/O.
// No copyrighted ROMs or disk images: all payloads are synthetic fixtures.
#include "../../upstream/standalone/shim/utf8_file_paths.h"
#include "../../upstream/runtime/eiii_runtime.h"
#include "../../upstream/runtime/wav_import.h"
#include "../../upstream/runtime/eiii_bank_import.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
using eiii::shim::utf8_from_path;
using eiii::shim::path_from_utf8;
namespace fs=std::filesystem;
static void check(bool ok,const std::string& text) { if(!ok) throw std::runtime_error(text); }
static void put(const fs::path& path,const std::vector<uint8_t>& data) {
 std::ofstream f(path,std::ios::binary|std::ios::trunc);
 check(bool(f),"write fixture failed");
 f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
 check(f.good(),"fixture data write failed");
}
static std::vector<uint8_t> small_wav(){
 std::vector<uint8_t> w(52,0);
 const auto word=[&](int off,unsigned x){w[off]=uint8_t(x);w[off+1]=uint8_t(x>>8);};
 const auto dword=[&](int off,unsigned x){word(off,x);word(off+2,x>>16);};
 const auto tag=[&](int off,const char* txt){for(int i=0;i<4;++i)w[off+i]=uint8_t(txt[i]);};
 tag(0,"RIFF");dword(4,44);tag(8,"WAVE");
 tag(12,"fmt ");dword(16,16);word(20,1);word(22,1);
 dword(24,44100);dword(28,88200);word(32,2);word(34,16);
 tag(36,"data");dword(40,8);
 word(44,0);word(46,16384);word(48,32768);word(50,65535);
 return w;
}
int main(){
 try {
#ifdef _WIN32
  std::printf("A13 active Windows ANSI code page: %u (UTF-8 is invariant)\n",unsigned(GetACP()));
#endif
  const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path base=fs::temp_directory_path()/fs::path("125A_unicode_"+std::to_string(stamp));
  fs::create_directory(base);
  struct Cleanup{fs::path p;~Cleanup()noexcept{std::error_code ec;fs::remove_all(p,ec);}} cleanup{base};
#ifdef _WIN32
  // Existing 125A song states from earlier builds may contain ACP bytes.
  // Only losslessly representable, invalid-UTF-8 strings are migrated.
  if(GetACP()!=CP_UTF8){
    const std::wstring oldWide=L"J\u00F6rg";
    BOOL replaced=FALSE;
    int n=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,
        oldWide.data(),int(oldWide.size()),nullptr,0,nullptr,&replaced);
    if(n>0&&!replaced){
      std::string legacy(size_t(n),'\0');
      replaced=FALSE;
      check(WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,
           oldWide.data(),int(oldWide.size()),legacy.data(),n,nullptr,&replaced)==n &&
           !replaced,"ACP fixture encoding");
      if(!eiii::shim::valid_utf8(legacy)){
        check(eiii::shim::upgrade_legacy_ansi_path(legacy),"ACP song path migration");
        check(legacy==utf8_from_path(fs::path(oldWide)),"ACP migration preserves characters");
      }
    }
  }
#endif
  const auto invalid=std::string("\xC3\x28",2);
  check(!eiii::shim::valid_utf8(invalid),"malformed UTF-8 accepted");
  check(path_from_utf8(invalid).empty(),"malformed UTF-8 path accepted");
  check(!eiii::shim::valid_utf8(std::string("file\0hidden",11)),"embedded NUL accepted");
  // These include ASCII, western codepage, characters outside Windows-1252
  // and a non-BMP surrogate pair. All must work regardless of GetACP().
  for(const wchar_t* folder : {L"ASCII",L"J\u00F6rg",L"\u65E5\u672C\u8A9E",L"\U0001F3B9"}){
    const auto native=base/fs::path(folder);
    fs::create_directory(native);
    const std::string utf8=utf8_from_path(native);
    check(!utf8.empty()&&eiii::shim::valid_utf8(utf8),"directory UTF-8 encoding");
    check(path_from_utf8(utf8)==native,"directory UTF-8 roundtrip");

    const fs::path rom=native/L"ROM", hd=native/L"EIII_\u00D6S.iso",
         cd=native/L"Library_\u65E5.iso", wav=native/L"V\u00F6gel_\u65E5.wav",
         dst=native/L"Export_\u65E5.img", bank=native/L"Scan_\u65E5.iso";
    fs::create_directory(rom);
    put(rom/L"e3-lsboot_ip381a_emu_systems_4088.ic3",std::vector<uint8_t>(16384,0));
    put(rom/L"e3-msboot_ip380a_emu_systems_4088.ic4",std::vector<uint8_t>(16384,0));
    put(hd,std::vector<uint8_t>(4096,0x21));
    put(cd,std::vector<uint8_t>(2048,0x42));
    put(wav,small_wav());
    put(bank,std::vector<uint8_t>(2048,0)); // not EMU3: scanner must return "no bank"
    std::string error;
    check(eiii::runtime::check_rom_dir(utf8_from_path(rom),error),"ROM Unicode open: "+error);
    error.clear();
    check(eiii::runtime::check_hd_image(utf8_from_path(hd),error),"HD Unicode open: "+error);
    error.clear();
    check(eiii::runtime::check_cd_image(utf8_from_path(cd),error),"CD Unicode open: "+error);
    eiii::wav::Audio audio;
    error.clear();
    check(eiii::wav::load(utf8_from_path(wav),audio,error),"WAV Unicode open: "+error);
    check(audio.frames()==4 && audio.rate==44100,"decoded WAV content");
    eiii::bank_import::disc_info info;
    error.clear();
    check(eiii::bank_import::scan(utf8_from_path(bank),info,error) &&
          !info.emu3,"bank scanner Unicode open");
    error.clear();
    check(eiii::runtime::write_image_with_changes(
       utf8_from_path(hd),{},utf8_from_path(dst),error),"HD Unicode export: "+error);
    check(fs::file_size(dst)==fs::file_size(hd),"HD Unicode export size");
    {
      FILE* f=eiii::shim::fopen_utf8(utf8_from_path(dst),"rb");
      check(f!=nullptr,"Unicode disk export fopen");
      check(std::fgetc(f)==0x21,"Unicode disk export contents");
      std::fclose(f);
    }
    // Bank cache/sidecar folder names stay UTF-8 through both conversions.
    const auto cache=eiii::bank_import::cached_path(
       utf8_from_path(bank),utf8_from_path(native/L"Cache_\u65E5"));
    check(eiii::shim::valid_utf8(cache),"bank cache name valid UTF-8");
    check(path_from_utf8(cache).parent_path()==native/L"Cache_\u65E5",
          "bank cache parent Unicode roundtrip");
  }
  std::puts("PASS A13: ASCII, Western umlaut, CJK, non-BMP emoji; ROM HD CD WAV bank scan, export, cache and invalid UTF-8");
  return 0;
 }catch(const std::exception& ex){
   std::fprintf(stderr,"FAIL A13: %s\n",ex.what());return 1;
 }
}
