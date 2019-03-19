#include <3ds.h>
#include "reslimit.h"
#include "util.h"
#include "manager.h"

typedef s64 ReslimitValues[10];

static const ResourceLimitType g_reslimitInitOrder[10] = {
    RESLIMIT_COMMIT,
    RESLIMIT_PRIORITY,
    RESLIMIT_THREAD,
    RESLIMIT_EVENT,
    RESLIMIT_MUTEX,
    RESLIMIT_SEMAPHORE,
    RESLIMIT_TIMER,
    RESLIMIT_SHAREDMEMORY,
    RESLIMIT_ADDRESSARBITER,
    RESLIMIT_CPUTIME,
};

static ReslimitValues g_o3dsReslimitValues[4] = {
    // APPLICATION
    {
        0x4000000,  // Allocatable memory
        0x18,       // Max. priority
        32,         // Threads
        32,         // Events
        32,         // Mutexes
        8,          // Semaphores
        8,          // Timers
        16,         // Shared memory objects
        2,          // Address arbiters
        0,          // Core1 CPU time
    },

    // SYS_APPLET
    {
        0x2606000,  // Allocatable memory
        4,          // Max priority
        14,         // Threads
        8,          // Events
        8,          // Mutexes
        4,          // Semaphores
        4,          // Timers
        8,          // Shared memory objects
        3,          // Address arbiters
        10000,      // Core1 CPU time
    },

    // LIB_APPLET
    {
        0x0602000,  // Allocatable memory
        4,          // Max priority
        14,         // Threads
        8,          // Events
        8,          // Mutexes
        4,          // Semaphores
        4,          // Timers
        8,          // Shared memory objects
        1,          // Address arbiters
        10000,      // Core1 CPU time
    },

    // OTHER (BASE sysmodules)
    {
        0x1682000,  // Allocatable memory
        4,          // Max priority
        202,        // Threads
        248,        // Events
        35,         // Mutexes
        64,         // Semaphores
        43,         // Timers
        30,         // Shared memory objects
        43,         // Address arbiters
        1000,       // Core1 CPU time
    },
};

static ReslimitValues g_n3dsReslimitValues[4] = {
    // APPLICATION
    {
        0x7C00000,  // Allocatable memory
        0x18,       // Max. priority
        32,         // Threads
        32,         // Events
        32,         // Mutexes
        8,          // Semaphores
        8,          // Timers
        16,         // Shared memory objects
        2,          // Address arbiters
        0,          // Core1 CPU time
    },

    // SYS_APPLET
    {
        0x5E06000,  // Allocatable memory
        4,          // Max priority
        29,         // Threads
        11,         // Events
        8,          // Mutexes
        4,          // Semaphores
        4,          // Timers
        8,          // Shared memory objects
        3,          // Address arbiters
        10000,      // Core1 CPU time
    },

    // LIB_APPLET
    {
        0x0602000,  // Allocatable memory
        4,          // Max priority
        14,         // Threads
        8,          // Events
        8,          // Mutexes
        4,          // Semaphores
        4,          // Timers
        8,          // Shared memory objects
        1,          // Address arbiters
        10000,      // Core1 CPU time
    },

    // OTHER (BASE sysmodules)
    {
        0x2182000,  // Allocatable memory
        4,          // Max priority
        225,        // Threads
        264,        // Events
        37,         // Mutexes
        67,         // Semaphores
        44,         // Timers
        31,         // Shared memory objects
        45,         // Address arbiters
        1000,       // Core1 CPU time
    },
};

static const struct {
    u32 titleUid;
    u32 value;
} g_startCpuTimeOverrides[] = {
    // Region-incoherent? CHN/KOR/TWN consistently missing.
    // Did it appear on 11.4 with the soundhax fix fix, or even before?
    { 0x205, 10000 }, // 3DS sound (JPN)
    { 0x215, 10000 }, // 3DS sound (USA)
    { 0x225, 10000 }, // 3DS sound (EUR)
    { 0x304, 10000 }, // Star Fox 64 3D (JPN)
    { 0x32E, 10000 }, // Super Monkey Ball 3D (JPN)
    { 0x334, 30    }, // Zelda no Densetsu: Toki no Ocarina 3D (JPN)
    { 0x335, 30    }, // The Legend of Zelda: Ocarina of Time 3D (USA)
    { 0x336, 30    }, // The Legend of Zelda: Ocarina of Time 3D (EUR)
    { 0x348, 10000 }, // Doctor Lautrec to Boukyaku no Kishidan (JPN)
    { 0x349, 10000 }, // Pro Yakyuu Spirits 2011 (JPN)
    { 0x368, 10000 }, // Doctor Lautrec and the Forgotten Knights (USA)
    { 0x370, 10000 }, // Super Monkey Ball 3D (USA)
    { 0x389, 10000 }, // Super Monkey Ball 3D (EUR)
    { 0x490, 10000 }, // Star Fox 64 3D (USA)
    { 0x491, 10000 }, // Star Fox 64 3D (EUR)
    { 0x562, 10000 }, // Doctor Lautrec and the Forgotten Knights (EUR)
};

static ReslimitValues *fixupReslimitValues(void)
{
    // In order: APPLICATION, SYS_APPLET, LIB_APPLET, OTHER
    // Fixup "commit" reslimit
    u32 sysmemalloc = SYSMEMALLOC;
    ReslimitValues *values = !IS_N3DS ? g_o3dsReslimitValues : g_n3dsReslimitValues;

    static const u32 minAppletMemAmount = 0x1200000;
    u32 defaultMemAmount = !IS_N3DS ? 0x2C00000 : 0x6400000;
    u32 otherMinOvercommitAmount = !IS_N3DS ? 0x280000 : 0x180000;
    u32 baseRegionSize = !IS_N3DS ? 0x1400000 : 0x2000000;

    if (sysmemalloc < minAppletMemAmount) {
        values[1][0] = SYSMEMALLOC - minAppletMemAmount / 3;
        values[2][0] = 0;
        values[3][0] = baseRegionSize + otherMinOvercommitAmount;
    } else {
        u32 excess = sysmemalloc < defaultMemAmount ? 0 : sysmemalloc - defaultMemAmount;
        values[1][0] = 3 * excess / 4 + sysmemalloc - minAppletMemAmount / 3;
        values[2][0] = 1 * excess / 4 + minAppletMemAmount / 3;
        values[3][0] = baseRegionSize + (otherMinOvercommitAmount + excess / 4);
    }

    values[0][0] = APPMEMALLOC;

    return values;
}

Result initializeReslimits(void)
{
    Result res = 0;
    ReslimitValues *values = fixupReslimitValues();
    for (u32 i = 0; i < 4; i++) {
        TRY(svcCreateResourceLimit(&g_manager.reslimits[i]));
        TRY(svcSetResourceLimitValues(g_manager.reslimits[i], g_reslimitInitOrder, values[i], 10));
    }

    return res;
}

static Result setAppCpuTimeLimit(s64 limit)
{
    ResourceLimitType category = RESLIMIT_CPUTIME;
    return svcSetResourceLimitValues(g_manager.reslimits[0], &category, &limit, 1);
}

static Result getAppCpuTimeLimit(s64 *limit)
{
    ResourceLimitType category = RESLIMIT_CPUTIME;
    return svcGetResourceLimitLimitValues(limit, g_manager.reslimits[0], &category, 1);
}

void setAppCpuTimeLimitAndSchedModeFromDescriptor(u64 titleId, u16 descriptor)
{
    /*
        Two main cases here:
            - app has a non-0 cputime descriptor in exhdr: current core1 cputime reslimit,
            and maximum, and scheduling mode are set to it. SetAppResourceLimit is *not* needed
            to use core1.
            - app has a 0 cputime descriptor: maximum is set to 80.
            Current reslimit is set to 0, and SetAppResourceLimit *is* needed
            to use core1, **EXCEPT** for an hardcoded set of titles (with a current reslimit
            possibly higher than the max=80?).

            Higher-than-100 values have special meanings (?).
    */
    u8 cpuTime = (u8)descriptor;
    assertSuccess(setAppCpuTimeLimit(cpuTime));

    g_manager.cpuTimeBase = 0;

    if (cpuTime != 0) {
        // Set core1 scheduling mode
        g_manager.maxAppCpuTime = cpuTime & 0x7F;
        assertSuccess(svcKernelSetState(6, 3, (cpuTime & 0x80) ? 1LL : 0LL));
    } else {
        u32 titleUid = ((u32)titleId >> 8) & 0xFFF;
        g_manager.maxAppCpuTime = 80;
        static const u32 numOverrides = sizeof(g_startCpuTimeOverrides) / sizeof(g_startCpuTimeOverrides[0]);

        if (titleUid >= g_startCpuTimeOverrides[0].titleUid && titleUid <= g_startCpuTimeOverrides[numOverrides - 1].titleUid) {
            u32 i = 0;
            for (u32 i = 0; i < numOverrides && titleUid < g_startCpuTimeOverrides[i].titleUid; i++);
            if (i < numOverrides) {
                if (g_startCpuTimeOverrides[i].value > 100 && g_startCpuTimeOverrides[i].value < 200) {
                    assertSuccess(svcKernelSetState(6, 3, 0LL));
                    assertSuccess(setAppCpuTimeLimit(g_startCpuTimeOverrides[i].value - 100));
                } else {
                    assertSuccess(svcKernelSetState(6, 3, 1LL));
                    assertSuccess(setAppCpuTimeLimit(g_startCpuTimeOverrides[i].value));
                }
            }
        }
    }
}

Result SetAppResourceLimit(u32 mbz, ResourceLimitType category, u32 value, u64 mbz2)
{
    if (mbz != 0 || mbz2 != 0 || category != RESLIMIT_CPUTIME || value > (u32)g_manager.maxAppCpuTime) {
        return 0xD8E05BF4;
    }

    value += value < 5 ? 0 : g_manager.cpuTimeBase;
    return setAppCpuTimeLimit(value);
}

Result GetAppResourceLimit(s64 *value, u32 mbz, ResourceLimitType category, u32 mbz2, u64 mbz3)
{
    if (mbz != 0 || mbz2 != 0 || mbz3 != 0 || category != RESLIMIT_CPUTIME) {
        return 0xD8E05BF4;
    }

    Result res = getAppCpuTimeLimit(value);
    if (R_SUCCEEDED(res) && *value >= 5) {
        *value = *value >= g_manager.cpuTimeBase ? *value - g_manager.cpuTimeBase : 0;
    }

    return res;
}
