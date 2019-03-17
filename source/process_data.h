#pragma once

#include <3ds/types.h>
#include <3ds/synchronization.h>
#include "intrusive_list.h"

typedef struct ProcessData {
    IntrusiveNode node;
    Handle handle;
    u32 pid;
    u64 titleId;
    u64 programHandle;
    u16 flags;
    u8 terminationStatus;
    u8 refcount; // note: 0-based (ie. it's 0 if it's an app or a dependency of a single process) ?
} ProcessData;

typedef struct ProcessDataList {
    RecursiveLock lock;
    IntrusiveList list;
    IntrusiveList freeList;
} ProcessDataList;

static inline void ProcessDataList_Lock(ProcessDataList *list)
{
    RecursiveLock_Lock(&list->lock);
}

static inline void ProcessDataList_Unlock(ProcessDataList *list)
{
    RecursiveLock_Unlock(&list->lock);
}

static inline ProcessData *ProcessDataList_GetNext(const ProcessData *process)
{
    return (ProcessData *)process->node.next;
}

static inline ProcessData *ProcessDataList_GetPrev(const ProcessData *process)
{
    return (ProcessData *)process->node.next;
}

static inline ProcessData *ProcessDataList_GetFirst(const ProcessDataList *list)
{
    return (ProcessData *)list->list.first;
}

static inline ProcessData *ProcessDataList_GetLast(const ProcessDataList *list)
{
    return (ProcessData *)list->list.last;
}

static inline bool ProcessDataList_TestEnd(const ProcessDataList *list, const ProcessData *process)
{
    return IntrusiveList_TestEnd(&list->list, &process->node);
}

static inline ProcessData *ProcessDataList_New(ProcessDataList *list)
{
    if (IntrusiveList_TestEnd(&list->freeList, list->freeList.first)) {
        return NULL;
    }

    IntrusiveNode *nd = list->freeList.first;
    IntrusiveList_Erase(nd);
    IntrusiveList_InsertAfter(list->list.last, nd);
    return (ProcessData *)nd;
}

static inline void ProcessDataList_Delete(ProcessDataList *list, ProcessData *process)
{
    IntrusiveList_Erase(&process->node);
    IntrusiveList_InsertAfter(list->freeList.first, &process->node);
}

static inline ProcessData *ProcessDataList_FindProcessById(const ProcessDataList *list, u32 pid)
{
    for (ProcessData *process = ProcessDataList_GetFirst(list); !ProcessDataList_TestEnd(list, process); process = ProcessDataList_GetNext(process)) {
        if (process->pid == pid) {
            return process;
        }
    }

    return NULL;
}

static inline ProcessData *ProcessDataList_FindProcessByHandle(const ProcessDataList *list, Handle handle)
{
    for (ProcessData *process = ProcessDataList_GetFirst(list); !ProcessDataList_TestEnd(list, process); process = ProcessDataList_GetNext(process)) {
        if (process->handle == handle) {
            return process;
        }
    }

    return NULL;
}
