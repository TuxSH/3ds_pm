#pragma once

#include <3ds/types.h>
#include "process_data.h"

typedef struct Manager {
    ProcessDataList processList;
    ProcessData *applicationData;
    Handle reslimits[4];
    Handle processTerminationEvent;
    bool waitingForTermination;
    bool preparingForReboot;
    u8 appCpuTime;
    u8 cpuTimeBase;
    Handle queuedApplicationProcessHandle;
} Manager;
