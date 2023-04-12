/*
 * This file is part of the OpenMV project.
 *
 * Copyright (c) 2013-2019 Ibrahim Abdelkader <iabdalkader@openmv.io>
 * Copyright (c) 2013-2019 Kwabena W. Agyeman <kwagyeman@openmv.io>
 *
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Fast approximate math functions.
 */
#ifndef __FMATH_H__
#define __FMATH_H__
#include <stdint.h>
#include "common.h"
#if defined(__CC_ARM) || defined(__CLANG_ARM) || (defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)) 
#include <math.h>
// #define fast_sqrtf sqrtf
static inline float fast_sqrtf(float x)
{
#if defined(__CC_ARM)	
	__asm {
		VSQRT.F32	x,	x
	}
#else
	asm volatile("vsqrt.f32 %[r], %[x]\n"
			:[r] "=t" (x)
			: [x] "t"  (x));
#endif	
    return x;
}

typedef union 
{
	float f32;
	int s32;
}U32Combo;

typedef union 
{
	double f64;
	long long s64;
}U64Combo;
#if 0
__asm static int fast_roundf(float x)
{
	vcvtr.s32.f32	s0, s0
	vmov	r0, s0
	bx		lr
	ALIGN
}


__asm static int  fast_floorf(float x)
{
	vcvt.s32.f32	s0, s0
	vmov	r0, s0
	bx		lr
	ALIGN
}
__asm static int fast_ceilf(float x)
{	
	vldr.f32	s1,=0.99999
	vadd.f32	s0,	s0,	s1
	vcvt.s32.f32	s0, s0
	vmov	r0, s0
	bx		lr
	ALIGN
}
#else
static inline int fast_ceilf(float x)
{	
    register float fi;
	register int i;
    x += 0.99999f;
#if defined(__CC_ARM)	
    __asm{
		vcvt.s32.f32 fi, x
		vmov  i, fi
    }
#else
	asm volatile (
            "vcvt.S32.f32  %[r], %[x]\n"
            : [r] "=t" (i)
            : [x] "t"  (x));
			
#endif	
    return i;
}

static inline int fast_floorf(float x)
{	
    register float fi;
	register int i;
#if defined(__CC_ARM)		
    __asm{
		vcvt.s32.f32 fi, x
		vmov  i, fi
    }
#else
	asm volatile (
            "vcvt.S32.f32  %[r], %[x]\n"
            : [r] "=t" (i)
            : [x] "t"  (x));
#endif	
    return i;
}

static inline int fast_roundf(float x)
{	
    register float fi;
	register int i;
#if defined(__CC_ARM)		
    __asm("");
	{
		vcvtr.s32.f32 fi, x
		vmov  i, fi
    }
#else
	asm volatile (
            "vcvtr.s32.f32  %[r], %[x]\n"
            : [r] "=t" (i)
            : [x] "t"  (x));
#endif	
    return i;
}

#endif
#define fast_fabsf fabs
#define isnanf __ARM_isnanf
#define isinff __ARM_isinff
#else
float fast_sqrtf(float x);
int fast_floorf(float x);
int fast_ceilf(float x);

#endif
int fast_roundf(float x);


float fast_atanf(float x);
float fast_atan2f(float y, float x);
float fast_expf(float x);
float fast_cbrtf(float d);
//float fast_fabsf(float d);
float fast_log(float x);
float fast_log2(float x);
float fast_powf(float a, float b);
extern const float cos_table[360];
extern const float sin_table[360];
#endif // __FMATH_H__
