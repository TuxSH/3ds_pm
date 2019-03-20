#pragma once

#include <3ds/types.h>
#include "process_data.h"

typedef struct Manager {
    ProcessList processList;
    ProcessData *applicationData;
    ProcessData *debugData; // note: official PM uses applicationData for both, and has queuedApplicationProcessHandle
    Handle reslimits[4];
    Handle processTerminationEvent;
    bool waitingForTermination;
    bool preparingForReboot;
    u8 maxAppCpuTime;
    s8 cpuTimeBase;
} Manager;

extern Manager g_manager;
