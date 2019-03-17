#include <3ds.h>
#include "reslimit.h"
#include "util.h"

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

Result initializeReslimits(Handle outResLimits[4])
{
    Result res = 0;
    ReslimitValues *values = fixupReslimitValues();
    for (u32 i = 0; i < 4; i++) {
        TRY(svcCreateResourceLimit(&outResLimits[i]));
        TRY(svcSetResourceLimitValues(outResLimits[i], g_reslimitInitOrder, values[i], 10));
    }

    return res;
}
