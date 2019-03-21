#include <3ds.h>
#include <string.h>
#include "process_monitor.h"
#include "exheader_info_heap.h"
#include "termination.h"
#include "manager.h"
#include "util.h"

static void cleanupProcess(ProcessData *process)
{
    if (process->flags & PROCESSFLAG_DEPENDENCIES_LOADED) {
        ExHeader_Info *exheaderInfo = ExHeaderInfoHeap_New();

        if (exheaderInfo == NULL) {
            panic(0);
        }

        listAndTerminateDependencies(process, exheaderInfo);

        ExHeaderInfoHeap_Delete(exheaderInfo);
    }

    if (!(process->flags & PROCESSFLAG_KIP)) {
        SRVPM_UnregisterProcess(process->pid);
        FSREG_Unregister(process->pid);
        LOADER_UnregisterProgram(process->programHandle);
    }

    if (process == g_manager.runningApplicationData) {
        g_manager.runningApplicationData = NULL;
    }

    if (process == g_manager.debugData) {
        g_manager.debugData = NULL;
    }

    if (process->flags & PROCESSFLAG_NOTIFY_TERMINATION) {
        notifySubscribers(0x110 + process->terminatedNotificationVariation);
    }
}

void processMonitor(void *p)
{
    (void)p;

    Handle handles[0x41] = { g_manager.newProcessEvent };

    for (;;) {
        u32 numProcesses = 0;
        bool atLeastOneTerminating = false;
        ProcessData *process;
        ProcessData processBackup;
        s32 id = -1;

        ProcessList_Lock(&g_manager.processList);
        FOREACH_PROCESS(&g_manager.processList, process) {
            // Rebuild the handle array
            if (process->terminationStatus != TERMSTATUS_TERMINATED) {
                handles[numProcesses++] = process->handle;
                if (process->terminationStatus == TERMSTATUS_NOTIFICATION_SENT) {
                    atLeastOneTerminating = true;
                }
            }
        }
        ProcessList_Unlock(&g_manager.processList);

        // If no more processes are terminating, signal the event
        if (g_manager.waitingForTermination && !atLeastOneTerminating) {
            assertSuccess(svcSignalEvent(g_manager.allNotifiedTerminationEvent));
        }

        // Note: lack of assertSuccess is intentional.
        svcWaitSynchronizationN(&id, handles, 1 + numProcesses, false, -1LL);

        if (id > 0) {
            // Note: official PM conditionally erases the process from the list, cleans up, then conditionally frees the process data
            // Bug in official PM (?): it unlocks the list before setting termstatus = TERMSTATUS_TERMINATED
            ProcessList_Lock(&g_manager.processList);
            process = ProcessList_FindProcessByHandle(&g_manager.processList, handles[id]);
            if (process != NULL) {
                process->terminationStatus = TERMSTATUS_TERMINATED;
                if (process->flags & PROCESSFLAG_NOTIFY_TERMINATION) {
                    process->flags |= PROCESSFLAG_NOTIFY_TERMINATION_TERMINATED;
                    processBackup = *process; // <-- make sure no list access is done through this node
                } else {
                    processBackup = *process; // <-- make sure no list access is done through this node
                    ProcessList_Delete(&g_manager.processList, process);
                }
            }
            ProcessList_Unlock(&g_manager.processList);

            if (process != NULL) {
                cleanupProcess(&processBackup);
                if (!(processBackup.flags & PROCESSFLAG_NOTIFY_TERMINATION_TERMINATED)) {
                    svcCloseHandle(processBackup.handle);
                }
                ProcessList_Lock(&g_manager.processList);
                ProcessList_Unlock(&g_manager.processList);
            }
        }
    }
}
