#include <3ds.h>
#include <string.h>
#include "launch.h"
#include "info.h"
#include "manager.h"
#include "reslimit.h"
#include "exheader_info_heap.h"
#include "task_runner.h"
#include "util.h"

static inline void removeAccessToService(const char *service, char (*serviceAccessList)[8])
{
    char ALIGN(8) name[8+1];
    strncpy(name, service, 8);
    name[8] = 0;

    u32 j = 0;
    for (u32 i = 0; i < 34 && *(u64 *)serviceAccessList[i] != 0; i++) {
        if (*(u64 *)serviceAccessList[i] != *(u64 *)name) {
            *(u64 *)serviceAccessList[j++] = *(u64 *)serviceAccessList[i];
        }
    }

    if (j < 34) {
        memset(&serviceAccessList[j], 0, 8 * (34 - j));
    }
}

static void blacklistServices(u64 titleId, char (*serviceAccessList)[8])
{
    if (osGetFirmVersion() < SYSTEM_VERSION(2, 51, 0) || (titleId >> 46) != 0x10) {
        return;
    }

    u32 titleUid = ((u32)titleId >> 8) & 0xFFFFF;

    switch (titleUid) {
        // Cubic Ninja
        case 0x343:
        case 0x465:
        case 0x4B3:
            removeAccessToService("http:C", serviceAccessList);
            removeAccessToService("soc:U", serviceAccessList);
            break;

        default:
            break;
    }
}

// Note: official PM doesn't include svcDebugActiveProcess in this function, but rather in the caller handling dependencies
static void loadWithoutDependencies(Handle *outDebug, ProcessData **outProcessData, u64 programHandle, const FS_ProgramInfo *programInfo,
    u32 launchFlags, const ExHeader_Info *exheaderInfo)
{
    Result res = 0;
    Handle processHandle = 0;
    u32 pid;
    ProcessData *process;
    const ExHeader_Arm11SystemLocalCapabilities *localcaps = &exheaderInfo->aci.local_caps;

    if (programInfo->programId & (1ULL << 35)) {
        // RequireBatchUpdate?
        return 0xD8E05803;
    }

    TRY(LOADER_LoadProcess(&processHandle, programHandle));
    TRY(svcGetProcessId(&pid, processHandle));

    // Note: bug in official PM: it seems not to panic/cleanup properly if the function calls below fail,
    // svcTerminateProcess won't be called, it's possible to trigger NULL derefs if you crash fs/sm/whatever,
    // leaks dependencies, and so on...
    // This can be solved by interesting the new process in the list earlier, etc. etc., allowing us to simplify the logic greatly.

    ProcessList_Lock(&g_manager.processList);
    process = ProcessList_New(&g_manager.processList);
    if (process == NULL) {
        panic(1);
    }

    process->pid = pid;
    process->programHandle = programHandle;
    process->refcount = 1;
    ProcessList_Unlock(&g_manager.processList);
    svcSignalEvent(g_manager.newProcessEvent);

    u32 serviceCount;
    for(serviceCount = 1; serviceCount <= 34 && *(u64 *)localcaps->service_access[serviceCount - 1] != 0; serviceCount++);

    TRYG(FSREG_Register(pid, programHandle, programInfo, localcaps->storage_info), cleanup);
    TRYG(SRVPM_RegisterProcess(pid, serviceCount, localcaps->service_access), cleanup);

    if (localcaps->reslimit_category <= RESLIMIT_CATEGORY_OTHER) {
        TRYG(svcSetProcessResourceLimits(processHandle, g_manager[localcaps->reslimit_category]), cleanup);
    }

    // Yes, even numberOfCores=2 on N3DS. On the 3DS, the affinity mask doesn't play the role of an access limiter,
    // it's only useful for cpuId < 0. thread->affinityMask = process->affinityMask | (cpuId >= 0 ? 1 << cpuId : 0)
    u8 affinityMask = localcaps->core_info.affinity_mask;
    TRYG(svcSetProcessAffinityMask(process, &affinityMask, 2), cleanup);
    TRYG(svcSetProcessIdealProcessor(processHandle, localcaps->core_info.ideal_processor), cleanup);

    setAppCpuTimeLimitAndSchedModeFromDescriptor(localcaps->title_id, localcaps->reslimits[0]);

    if (launchFlags & PMLAUNCHFLAG_QUEUE_DEBUG_APPLICATION) {
        TRYG(svcDebugActiveProcess(outDebug, pid), cleanup);
    }

    cleanup:
    if (R_FAILED(res)) {
        svcTerminateProcess(processHandle); // missing in official pm in all code paths but one
        return res;
    }

    process->flags = (launchFlags & PMLAUNCHFLAG_NOTIFY_TERMINATION) ? PROCESSFLAG_NOTIFY_TERMINATION : 0;

    if (outProcessData != NULL) {
        *outProcessData = process;
    }

    return res;
}

static void loadWithDependencies(Handle *outDebug, ProcessData **outProcessData, u64 programHandle, const FS_ProgramInfo *programInfo,
    u32 launchFlags, const ExHeader_Info *exheaderInfo)
{
    Result res = 0;

    struct {
        u64 titleId;
        u32 count;
        ProcessData *process;
    } dependenciesInfo[48] = {0};
    u32 totalNumDeps, numNewDeps = 0, numDeps = 0;

    //u32 offset = 0;
    u64 currentDeps[48]; // note: official PM reuses exheader to save stack space but we have enough of it

    Result res = 0;
    TRY(loadWithoutDependencies(outDebug, outProcessData, programHandle, programInfo, launchFlags, exheaderInfo));
    ProcessData *process = *outProcessData;


    listDependencies(currentDeps, &numDeps, process, exheaderInfo, false);
    for (u32 i = 0; i < numDeps; i++) {
        // Filter duplicate results
        u32 j;
        for (j = 0; j < numNewDeps && currentDeps[i] != dependenciesInfo[j].titleId; j++);
        if (j >= numNewDeps) {
            dependenciesInfo[numNewDeps++].titleId = currentDeps[i];
            dependenciesInfo[numNewDeps].count = 1;
        } else {
            ++dependenciesInfo[j].count;
        }
    }

    ExHeader_Info *depExheaderInfo = NULL;
    FS_ProgramInfo depProgramInfo;

    if (numDeps == 0) {
        goto add_dup_refs; // official pm forgets to do that
    }

    process->flags |= PROCESSFLAG_DEPENDENCIES_LOADED;
    depExheaderInfo = ExHeaderInfoHeap_New();
    if (depExheaderInfo = NULL) {
        panic(0);
    }

    /*
        Official pm does this:
            for each dependency:
                if dep already loaded: if autoloaded increase refcount // note: not autoloaded = not autoterminated
                else: load new sysmodule w/o its deps (then process its deps), set flag "autoloaded"  return early from entire function if it fails
        Naturally, it forgets to incref all subsequent dependencies here & also when it factors the duplicate entries in

        It also has a buffer overflow bug if the flattened dep tree has more than 48 elements (but this can never happen in practice)
    */

    for (totalNumDeps = 0; numNewDeps != 0; ) {
        if (totalNumDeps + numNewDeps > 48) {
            panic(2);
        }

        // Look up for existing processes
        ProcessList_Lock(&g_manager.processList);
        for (u32 i = 0; i < numNewDeps; i++) {
            FOREACH_PROCESS(&g_manager.processList, process) {
                if (process->titleId & ~0xFF == dependenciesInfo[totalNumDeps + i] & ~0xFF) {
                    // Note: two processes can't have the same normalized titleId
                    dependenciesInfo[totalNumDeps + i].process = process;
                    if (process->flags & PROCESSFLAG_AUTOLOADED) {
                        if (process->refcount = 0xFF) {
                            panic(3);
                        }
                        ++process->refcount;
                    }
                }
            }
        }
        ProcessList_Unlock(&g_manager.processList);

        // Launch the newly needed sysmodules, handle dependencies here and not in the function call to avoid infinite recursion
        u32 currentNumNewDeps = numNewDeps;
        for (u32 i = 0; i < currentNumNewDeps; i++) {
            if (dependenciesInfo[totalNumDeps + i].process != NULL) {
                continue;
            }

            depProgramInfo.programId = dependenciesInfo[i].titleId;
            depProgramInfo.mediaType = MEDIATYPE_NAND;

            TRYG(launchTitleImpl(NULL, &process, &depProgramInfo, NULL, 0, depExheaderInfo), add_dup_refs);

            listDependencies(currentDeps, &numDeps, &process, depExheaderInfo, false);
            process->flags |= PROCESSFLAG_AUTOLOADED | PROCESSFLAG_DEPENDENCIES_LOADED;

            for (u32 i = 0; i < numDeps; i++) {
                // Filter existing results
                u32 j;
                for (j = 0; j < totalNumDeps + numNewDeps && currentDeps[i] != dependenciesInfo[j].titleId; j++);
                if (j >= totalNumDeps + numNewDeps) {
                    dependenciesInfo[totalNumDeps + numNewDeps++].titleId = currentDeps[i];
                    dependenciesInfo[totalNumDeps + numNewDeps].count = 1;
                } else {
                    ++dependenciesInfo[j].count;
                }
            }
        }

        totalNumDeps += currentNumNewDeps;
    }

    add_dup_refs:
    for (u32 i = 0; i < totalNumDeps; i++) {
        if (dependenciesInfo[i].process != NULL && (dependenciesInfo[i].process->flags & PROCESSFLAG_AUTOLOADED) != 0) {
            if (dependenciesInfo[i].process->refcount + dependenciesInfo[i].count - 1 >= 0x100) {
                panic(3);
            }

            dependenciesInfo[i].process->refcount += dependenciesInfo[i].count - 1;
        }
    }

    if (depExheaderInfo != NULL) {
        ExHeaderInfoHeap_Delete(depExheaderInfo);
    }

    return res;
}

Result launchTitleImpl(Handle *debug, ProcessData **outProcessData, const FS_ProgramInfo *programInfo,
    const FS_ProgramInfo *programInfoUpdate, u32 launchFlags, const ExHeader_Info *exheaderInfo)
{
    if (launchFlags & PMLAUNCHFLAG_NORMAL_APPLICATION) {
        launchFlags |= PMLAUNCHFLAG_LOAD_DEPENDENCIES;
    } else {
        launchFlags &= ~(PMLAUNCHFLAG_USE_UPDATE_TITLE | PMLAUNCHFLAG_QUEUE_DEBUG_APPLICATION);
    }

    Result res = 0;
    u64 programHandle;
    StartupInfo si = {0};

    programInfoUpdate = (launchFlags & PMLAUNCHFLAG_USE_UPDATE_TITLE) ? programInfoUpdate : programInfo;
    TRY(LOADER_RegisterProgram(&programHandle, programInfo->programId, programInfo->mediaType,
        programInfoUpdate->programId, programInfoUpdate->mediaType));

    res = LOADER_GetProgramInfo(exheaderInfo, programHandle);
    res = R_SUCCEEDED(res) && exheaderInfo->aci.local_caps.core_info.core_version != SYSCOREVER ? 0xC8A05800 : res;

    if (R_FAILED(res)) {
        LOADER_UnregisterProgram(programHandle);
        return res;
    }

    blacklistServices(exheaderInfo->aci.local_caps.title_id, exheaderInfo->aci.local_caps.service_access);

    if (launchFlags & PMLAUNCHFLAG_LOAD_DEPENDENCIES) {
        TRY(loadWithDependencies(debug, outProcessData, programHandle, programInfo, launchFlags, exheaderInfo));
    } else {
        TRY(loadWithoutDependencies(debug, outProcessData, programHandle, programInfo, launchFlags, exheaderInfo));
    }
}
