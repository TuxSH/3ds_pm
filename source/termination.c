#include <3ds.h>
#include "termination.h"
#include "manager.h"
#include "util.h"
#include "exheader_info_heap.h"

static Result terminateUnusedDependencies(const u64 *dependencies, u32 numDeps)
{
    ProcessData *process;
    Result res = 0;

    ProcessList_Lock(&g_manager.processList);
    FOREACH_PROCESS(&g_manager.processList, process) {
        if (process->terminationStatus == TERMSTATUS_RUNNING || !(process->flags & PROCESSFLAG_AUTOLOADED)) {
            continue;
        }

        u32 i;
        for (i = 0; i < numDeps && dependencies[i] != process->titleId; i++);

        if (i >= numDeps || --process->refcount > 0) {
            // Process not a listed dependency or still used
            continue;
        }

        res = ProcessData_SendTerminationNotification(process);
        res = R_SUMMARY(res) == RS_NOTFOUND ? 0 : res;

        if (R_FAILED(res)) {
            assertSuccess(svcTerminateProcess(process->handle));
        }
    }

    ProcessList_Unlock(&g_manager.processList);
    return res;
}

Result listAndTerminateDependencies(ProcessData *process, ExHeader_Info *exheaderInfo)
{
    Result res;

    TRY(LOADER_GetProgramInfo(exheaderInfo, process->programHandle));

    u32 numDeps = 0;
    u64 dependencies[48];
    for (u32 i = 0; i < 48 && exheaderInfo->sci.dependencies[i] != 0; i++) {
        u64 titleId = exheaderInfo->sci.dependencies[i];
        if (IS_N3DS || (titleId & 0xF0000000) == 0) {
            // On O3DS, ignore N3DS titles.
            // Then (on both) remove the N3DS titleId bits
            dependencies[numDeps++] = titleId & ~0xF0000000;
        }
    }

    return terminateUnusedDependencies(dependencies, numDeps);
}

void commitPendingTerminations(s64 timeout)
{
    // Wait for processes that have received notification 0x100 to terminate
    // till the timeout, then actually terminate these processes, etc.

    bool atLeastOneListener = false;
    ProcessList_Lock(&g_manager.processList);

    ProcessData *process;
    FOREACH_PROCESS(&g_manager.processList, process) {
        switch (process->terminationStatus) {
            case TERMSTATUS_NOTIFICATION_SENT:
                atLeastOneListener = true;
                break;
            case TERMSTATUS_NOTIFICATION_FAILED:
                assertSuccess(svcTerminateProcess(process->handle));
                break;
            default:
                break;
        }
    }

    ProcessList_Unlock(&g_manager.processList);

    if (atLeastOneListener) {
        Result res = assertSuccess(svcWaitSynchronization(g_manager.processTerminationEvent, timeout));

        if (R_DESCRIPTION(res) == RD_TIMEOUT) {
            ProcessList_Lock(&g_manager.processList);

            ProcessData *process;
            FOREACH_PROCESS(&g_manager.processList, process) {
                if (process->terminationStatus == TERMSTATUS_NOTIFICATION_SENT) {
                    assertSuccess(svcTerminateProcess(process->handle));
                }
            }

            ProcessList_Unlock(&g_manager.processList);

            assertSuccess(svcWaitSynchronization(g_manager.processTerminationEvent, timeout));
        }
    }
}
