#include <3ds.h>
#include "process_data.h"
#include "util.h"

ProcessData *ProcessList_FindProcessById(const ProcessList *list, u32 pid)
{
    ProcessData *process;

    FOREACH_PROCESS(list, process) {
        if (process->pid == pid) {
            return process;
        }
    }

    return NULL;
}

ProcessData *ProcessList_FindProcessByHandle(const ProcessList *list, Handle handle)
{
    ProcessData *process;

    FOREACH_PROCESS(list, process) {
        if (process->handle == handle) {
            return process;
        }
    }

    return NULL;
}

Result ProcessData_Notify(const ProcessData *process, u32 notificationId)
{
    Result res = SRVPM_PublishToProcess(notificationId, process->handle);
    if (res == (Result)0xD8606408) {
        panic(res);
    }

    return res;
}

Result ProcessData_SendTerminationNotification(ProcessData *process)
{
    Result res = ProcessData_Notify(process, 0x100);
    process->terminationStatus = R_SUCCEEDED(res) ? TERMSTATUS_NOTIFICATION_SENT : TERMSTATUS_NOTIFICATION_FAILED;
    return res;
}
