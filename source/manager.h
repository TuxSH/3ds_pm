#pragma once

#include <3ds/types.h>
#include "process_data.h"

typedef struct Manager {
    ProcessList processList;
    ProcessData *applicationData;
    Handle reslimits[4];
    Handle processTerminationEvent;
    bool waitingForTermination;
    bool preparingForReboot;
    u8 appCpuTime;
    s8 cpuTimeBase;
    Handle queuedApplicationProcessHandle;
} Manager;

extern Manager g_manager;
