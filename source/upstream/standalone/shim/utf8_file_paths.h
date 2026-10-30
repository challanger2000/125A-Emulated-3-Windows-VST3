#pragma once
// 125A EIII A13: all std::string *file paths* crossing the emulator/GUI
// boundary are strict UTF-8, not the process ANSI code page.
// Filename bytes inside original EIII banks remain their firmware encoding.
#include <cstdio>
#include <climits>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace eiii::shim {
inline bool valid_utf8(std::string_view value) noexcept {
#ifdef _WIN32
    if(value.find('\0')!=std::string_view::npos)return false;
    return value.size() <= static_cast<size_t>(INT_MAX) &&
           (value.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                value.data(),static_cast<int>(value.size()),nullptr,0)>0);
#else
    // Native POSIX file APIs are byte-oriented. UTF-8 validity is still
    // required at the file-boundary to match the project-state contract.
    if(value.find('\0')!=std::string_view::npos)return false;
    size_t i=0;
    while(i<value.size()){
        unsigned char c=static_cast<unsigned char>(value[i++]);
        if(c<0x80)continue;
        int rest=0;unsigned code=0,min=0;
        if((c&0xe0)==0xc0){rest=1;code=c&31;min=0x80;}
        else if((c&0xf0)==0xe0){rest=2;code=c&15;min=0x800;}
        else if((c&0xf8)==0xf0){rest=3;code=c&7;min=0x10000;}
        else return false;
        while(rest--){
            if(i>=value.size())return false;
            c=static_cast<unsigned char>(value[i++]);
            if((c&0xc0)!=0x80)return false;
            code=(code<<6)|(c&63);
        }
        if(code<min||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }
    return true;
#endif
}
inline std::filesystem::path path_from_utf8(std::string_view input) {
    if(!valid_utf8(input))return {};
#ifdef _WIN32
    if(input.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
          input.data(),static_cast<int>(input.size()),nullptr,0);
    if(n<=0)return {};
    std::wstring wide(size_t(n),L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
         input.data(),static_cast<int>(input.size()),wide.data(),n)!=n)return {};
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(input);
#endif
}
inline std::string utf8_from_path(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide=path.native();
    if(wide.empty()||wide.size()>static_cast<size_t>(INT_MAX))return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
        wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string out(size_t(n),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
        wide.data(),static_cast<int>(wide.size()),out.data(),n,nullptr,nullptr)!=n)return {};
    return out;
#else
    const auto u=path.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()),u.size());
#endif
}
// Compatibility migration for existing EIII-UI project-state paths saved
// by the old CP_ACP Windows file dialog. Only invalid UTF-8 bytes qualify:
// ASCII and valid UTF-8 state are preserved byte-for-byte. The codepage
// roundtrip rejects replacement/default and best-fit substitutions. It
// cannot disambiguate a coincidentally valid UTF-8 byte sequence from ACP.
inline bool upgrade_legacy_ansi_path(std::string& value) {
    if(value.empty()||valid_utf8(value))return true;
#ifdef _WIN32
    if(value.size()>static_cast<size_t>(INT_MAX)||GetACP()==CP_UTF8)return false;
    const int count=static_cast<int>(value.size());
    const int chars=MultiByteToWideChar(CP_ACP,0,value.data(),count,nullptr,0);
    if(chars<=0)return false;
    std::wstring wide(size_t(chars),L'\0');
    if(MultiByteToWideChar(CP_ACP,0,value.data(),count,wide.data(),chars)!=chars)
        return false;
    BOOL replaced=FALSE;
    const int encoded=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,
         wide.data(),chars,nullptr,0,nullptr,&replaced);
    if(encoded!=count||replaced)return false;
    std::string again(size_t(encoded),'\0');
    replaced=FALSE;
    if(WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,
         wide.data(),chars,again.data(),encoded,nullptr,&replaced)!=encoded ||
         replaced||again!=value)return false;
    std::string migrated=utf8_from_path(std::filesystem::path(wide));
    if(migrated.empty()||!valid_utf8(migrated))return false;
    value=std::move(migrated);
    return true;
#else
    return false;
#endif
}
inline FILE* fopen_utf8(const std::string& path,const char* mode) noexcept {
    if(path.empty()||!mode||!valid_utf8(path))return nullptr;
#ifdef _WIN32
    const auto wide=path_from_utf8(path);
    if(wide.empty())return nullptr;
    wchar_t wmode[12]{};
    size_t n=0;
    for(;mode[n]&&n<11;++n){
        if(static_cast<unsigned char>(mode[n])>=128)return nullptr;
        wmode[n]=static_cast<wchar_t>(mode[n]);
    }
    if(mode[n])return nullptr;
    return _wfopen(wide.c_str(),wmode);
#else
    return std::fopen(path.c_str(),mode);
#endif
}
inline int remove_utf8(const std::string& path) noexcept {
#ifdef _WIN32
    const auto wide=path_from_utf8(path);
    return wide.empty() ? -1 : _wremove(wide.c_str());
#else
    return std::remove(path.c_str());
#endif
}
} // namespace eiii::shim
