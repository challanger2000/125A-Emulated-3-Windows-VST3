#include "../../upstream/standalone/shim/large_file_io.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <limits>
int main() {
 FILE *f=tmpfile();assert(f);
 const std::uint64_t off=std::uint64_t(3)*1024*1024*1024;
 assert(eiii::shim::seek(f,off));
 assert(fputc(0x5a,f)==0x5a);
 fflush(f);
 std::uint64_t size=0;
 assert(eiii::shim::file_size(f,size) && size==off+1);
 assert(eiii::shim::seek(f,off) && fgetc(f)==0x5a);
 assert(!eiii::shim::seek(f,std::numeric_limits<std::uint64_t>::max()));
 fclose(f);
 puts("PASS 64-bit image I/O >2 GiB and invalid seek");
}
