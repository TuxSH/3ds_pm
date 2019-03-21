#pragma once

#include <3ds/types.h>
#include "process_data.h"

typedef struct Manager {
    ProcessList processList;
    ProcessData *runningApplicationData;
    ProcessData *debugData; // note: official PM uses runningApplicationData for both, and has queuedApplicationProcessHandle
    Handle reslimits[4];
    Handle newProcessEvent;
    Handle processTerminationEvent;
    bool waitingForTermination;
    bool preparingForReboot;
    u8 maxAppCpuTime;
    s8 cpuTimeBase;
} Manager;

extern Manager g_manager;
