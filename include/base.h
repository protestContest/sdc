#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint8_t u8;
typedef int8_t i8;
typedef float f32;
typedef double f64;

#define ArrayCount(a)   (sizeof (a) / sizeof (a)[0])
#define BlockMove(src,dst,sz)  memcpy(dst, src, sz)
#define Min(a, b)   ((a) < (b) ? (a) : (b))
#define Max(a, b)   ((a) > (b) ? (a) : (b))
#define Abs(n)      ((n) < 0 ? -(n) : (n))
#define Align(n, s) (((n)-1)/(s)+1)
