#include <3ds.h>
#include "manager.h"
#include "reslimit.h"
#include "util.h"

Manager g_manager;

void Manager_Init(void *procBuf, size_t numProc)
{
    memset(&g_manager, 0, sizeof(Manager));
    ProcessList_Init(&g_manager.processList, procBuf, numProc);
    assertSuccess(svcCreateEvent(&g_manager.newProcessEvent, RESET_ONESHOT));
    assertSuccess(svcCreateEvent(&g_manager.allNotifiedTerminationEvent , RESET_ONESHOT));
}

void Manager_RegisterKips(void)
{
    s64 numKips = 0;
    ProcessData *process;
    Handle processHandle;

    svcGetSystemInfo(&numKips, 26, 0);

    ProcessList_Lock(&g_manager.processList);
    for (u32 i = 0; i < (u32)numKips; i++) {
        process = ProcessList_New(&g_manager.processList);
        if (process == NULL) {
            panic(1);
        }

        assertSuccess(svcOpenProcess(&processHandle, i));
        process->handle = processHandle;
        process->pid = i;
        process->refcount = 1;
        process->titleId = 0x0004000100001000ULL; // note: same TID for all builtins
        process->flags = PROCESSFLAG_KIP;

        assertSuccess(svcSetProcessResourceLimits(processHandle, g_manager.reslimits[RESLIMIT_CATEGORY_OTHER]));
    }

    ProcessList_Unlock(&g_manager.processList);
}

Result UnregisterProcess(u64 titleId)
{
    ProcessData *process, *foundProcess = NULL;

    ProcessList_Lock(&g_manager.processList);

    FOREACH_PROCESS(&g_manager.processList, process) {
        if ((process->titleId & ~0xFF) == (titleId & ~0xFF)) {
            foundProcess = process;
            break;
        }
    }

    if (foundProcess != NULL) {
        if (foundProcess == g_manager.runningApplicationData) {
            g_manager.runningApplicationData = NULL;
        }

        if (foundProcess == g_manager.debugData) {
            g_manager.debugData = NULL;
        }

        svcCloseHandle(foundProcess->handle);
        ProcessList_Delete(&g_manager.processList, foundProcess);
    }

    ProcessList_Unlock(&g_manager.processList);
    return 0;
}
