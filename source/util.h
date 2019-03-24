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
#define N3DS_TID_MASK           0xF0000000ULL
#define N3DS_TID_BIT            0x20000000ULL

#define TRY(expr)               if(R_FAILED(res = (expr))) return res;
#define TRYG(expr, label)       if(R_FAILED(res = (expr))) goto label;

#define XDS
#ifdef XDS
static void hexItoa(u64 number, char *out, u32 digits, bool uppercase)
{
    const char hexDigits[] = "0123456789ABCDEF";
    const char hexDigitsLowercase[] = "0123456789abcdef";
    u32 i = 0;

    while(number > 0)
    {
        out[digits - 1 - i++] = uppercase ? hexDigits[number & 0xF] : hexDigitsLowercase[number & 0xF];
        number >>= 4;
    }

    while(i < digits) out[digits - 1 - i++] = '0';
}
#endif

static void __attribute__((noinline)) panic(Result res)
{
#ifndef XDS
    (void)res;
    __builtin_trap();
#else
    char buf[32] = {0};
    hexItoa(res, buf, 8, false);
    svcOutputDebugString(buf, 8);
    svcBreak(USERBREAK_PANIC);
#endif
}

static inline Result assertSuccess(Result res)
{
    if(R_FAILED(res)) {
        panic(res);
    }

    return res;
}

static inline Result notifySubscribers(u32 notificationId)
{
    Result res = srvPublishToSubscriber(notificationId, 0);
    if (res == (Result)0xD8606408) {
        panic(res);
    }

    return res;
}

static inline s64 nsToTicks(s64 ns)
{
    return ns * SYSCLOCK_ARM11 / (1000 * 1000 * 1000LL);
}

static inline s64 ticksToNs(s64 ticks)
{
    return 1000 * 1000 * 1000LL * ticks / SYSCLOCK_ARM11;
}
