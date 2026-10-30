// Native in-process test only. Never linked into the shipped VST3 module.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
namespace state_fault_test { inline thread_local std::ptrdiff_t budget=-1; }
void* operator new(std::size_t n) {
 if(state_fault_test::budget==0)throw std::bad_alloc();
 if(state_fault_test::budget>0)--state_fault_test::budget;
 if(void* p=std::malloc(n?n:1))return p;
 throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
