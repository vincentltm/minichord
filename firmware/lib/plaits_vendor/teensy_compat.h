#pragma once
// Teensy 4.0 platform compatibility shim for Rings DSP
// Replaces STM32-specific memory section attributes

#include <cmath>
#include <cstring>
#include <algorithm>

// Keep Plaits DSP functions in Flash memory to preserve ITCM RAM
#ifdef FASTRUN
#undef FASTRUN
#endif
#define FASTRUN FLASHMEM
#ifndef IN_RAM
#define IN_RAM FLASHMEM
#endif

// ARM CMSIS saturation intrinsic (available on Cortex-M7)
#ifndef __SSAT
#define __SSAT(x, bits) \
  ((x) > ((1 << ((bits)-1)) - 1) ? ((1 << ((bits)-1)) - 1) : \
   ((x) < -(1 << ((bits)-1)) ? -(1 << ((bits)-1)) : (x)))
#endif

// Undefine Teensy Audio Library WAVEFORM_* macros to avoid collision with Plaits enums
#ifdef WAVEFORM_SINE
#undef WAVEFORM_SINE
#endif
#ifdef WAVEFORM_SQUARE
#undef WAVEFORM_SQUARE
#endif
#ifdef WAVEFORM_TRIANGLE
#undef WAVEFORM_TRIANGLE
#endif
#ifdef WAVEFORM_SAWTOOTH
#undef WAVEFORM_SAWTOOTH
#endif

