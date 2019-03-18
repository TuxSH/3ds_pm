#pragma once

#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/os.h>

#define REG32(reg)              (*(vu32 *)reg)
#define REG64(reg)              (*(vu64 *)reg)

#define NSTID                   REG64(0x1FF80008)
#define SYSCOREVER              REG32(0x1FF80010)
#define APPMEMTYPE              REG32(0x1FF80030)
#define APPMEMALLOC             REG32(0x1FF80040)
#define SYSMEMALLOC             REG32(0x1FF80044)

#define IS_N3DS                 (*(vu32 *)0x1FF80030 >= 6) // APPMEMTYPE. Hacky but doesn't use APT

#define TRY(expr)               if(R_FAILED(res = (expr))) return res;

static void panic(Result res)
{
    (void)res;
    __builtin_trap();
}

static inline Result assertSuccess(Result res)
{
    if(R_FAILED(res)) {
        panic(res);
    }

    return res;
}

