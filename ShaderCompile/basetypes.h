#pragma once

#include <cstdint>
#ifdef _DEBUG
	#include <cassert>
#endif

#ifdef _DEBUG
	#define Assert assert
#else
	#define Assert(e)
#endif

#if !defined(_WIN32) && !defined(_WIN64)
	#include "compat_linux.h"
#endif

#if !defined(_MSC_VER)
  #if defined(__clang__)
    #define __assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
  #elif defined(__GNUC__)
    #define __assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
  #else
    #define __assume(x) ((void)0)
  #endif
#endif
