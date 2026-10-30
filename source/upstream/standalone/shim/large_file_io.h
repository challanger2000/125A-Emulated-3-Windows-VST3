#pragma once
#include <cstdint>
#include <cstdio>
#include <limits>
namespace eiii::shim {
inline bool seek(FILE* f, std::uint64_t offset) noexcept {
 if (!f) return false;
#if defined(_WIN32)
 if (offset > std::uint64_t(std::numeric_limits<__int64>::max())) return false;
 return _fseeki64(f, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
 if (offset > std::uint64_t(std::numeric_limits<off_t>::max())) return false;
 return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}
inline bool file_size(FILE* f, std::uint64_t& size) noexcept {
 if(!f) return false;
#if defined(_WIN32)
 if (_fseeki64(f,0,SEEK_END) != 0) return false;
 const auto pos=_ftelli64(f);
#else
 if (fseeko(f,0,SEEK_END) != 0) return false;
 const auto pos=ftello(f);
#endif
 if(pos < 0) return false;
 size=static_cast<std::uint64_t>(pos);
 return true;
}
}
