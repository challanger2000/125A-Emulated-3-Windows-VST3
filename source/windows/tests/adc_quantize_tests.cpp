#include "../../upstream/runtime/adc_quantize.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
int main() {
 using eiii::adc::quantize;
 assert(quantize(0,1)==0);
 assert(quantize(1,1)==32767);
 assert(quantize(-1,1)==-32767);
 assert(quantize(0.5,1)==16384);
 assert(quantize(-0.5,1)==-16384);
 assert(quantize(1000000000.0,100)==32767);
 assert(quantize(-1000000000.0,100)==-32768);
 assert(quantize(std::numeric_limits<double>::max(),100)==32767);
 assert(quantize(-std::numeric_limits<double>::max(),100)==-32768);
 assert(quantize(std::numeric_limits<double>::infinity(),1)==0);
 assert(quantize(std::numeric_limits<double>::quiet_NaN(),1)==0);
 assert(quantize(1,std::numeric_limits<double>::infinity())==0);
 puts("PASS ADC conversion: saturation before rounding, huge/nonfinite inputs");
}
