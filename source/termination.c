#include <3ds.h>
#include "termination.h"
#include "manager.h"
#include "util.h"

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
