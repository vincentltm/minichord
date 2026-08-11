#pragma once
// Teensy 4.0 platform compatibility shim for Rings DSP
// Replaces STM32-specific memory section attributes

#include <cmath>
#include <cstring>
#include <algorithm>

// Teensy 4.0 uses FASTRUN for performance-critical code
#ifndef IN_RAM
#define IN_RAM FASTRUN
#endif

// ARM CMSIS saturation intrinsic (available on Cortex-M7)
#ifndef __SSAT
#define __SSAT(x, bits) \
  ((x) > ((1 << ((bits)-1)) - 1) ? ((1 << ((bits)-1)) - 1) : \
   ((x) < -(1 << ((bits)-1)) ? -(1 << ((bits)-1)) : (x)))
#endif
