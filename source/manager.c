#include <3ds.h>
#include "manager.h"
#include "reslimit.h"

Manager g_manager = {};

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
