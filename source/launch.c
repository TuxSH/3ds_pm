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

// Note: official PM has two distinct functions for sysmodule vs. regular app. We refactor that into a single function.
static Result launchTitleImpl(Handle *debug, ProcessData **outProcessData, const FS_ProgramInfo *programInfo,
    const FS_ProgramInfo *programInfoUpdate, u32 launchFlags, ExHeader_Info *exheaderInfo);

// Note: official PM doesn't include svcDebugActiveProcess in this function, but rather in the caller handling dependencies
static Result loadWithoutDependencies(Handle *outDebug, ProcessData **outProcessData, u64 programHandle, const FS_ProgramInfo *programInfo,
    u32 launchFlags, const ExHeader_Info *exheaderInfo)
{
    Result res = 0;
    Handle processHandle = 0;
    u32 pid;
    ProcessData *process;
    const ExHeader_Arm11SystemLocalCapabilities *localcaps = &exheaderInfo->aci.local_caps;

    if (outProcessData != NULL) {
        *outProcessData = NULL;
    }

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

    process->handle = processHandle;
    process->pid = pid;
    process->programHandle = programHandle;
    process->refcount = 1;
    ProcessList_Unlock(&g_manager.processList);
    svcSignalEvent(g_manager.newProcessEvent);

    u32 serviceCount;
    for(serviceCount = 1; serviceCount <= 34 && *(u64 *)localcaps->service_access[serviceCount - 1] != 0; serviceCount++);

    TRYG(FSREG_Register(pid, programHandle, programInfo, &localcaps->storage_info), cleanup);
    TRYG(SRVPM_RegisterProcess(pid, serviceCount, localcaps->service_access), cleanup);

    if (localcaps->reslimit_category <= RESLIMIT_CATEGORY_OTHER) {
        TRYG(svcSetProcessResourceLimits(processHandle, g_manager.reslimits[localcaps->reslimit_category]), cleanup);
    }

    // Yes, even numberOfCores=2 on N3DS. On the 3DS, the affinity mask doesn't play the role of an access limiter,
    // it's only useful for cpuId < 0. thread->affinityMask = process->affinityMask | (cpuId >= 0 ? 1 << cpuId : 0)
    u8 affinityMask = localcaps->core_info.affinity_mask;
    TRYG(svcSetProcessAffinityMask(processHandle, &affinityMask, 2), cleanup);
    TRYG(svcSetProcessIdealProcessor(processHandle, localcaps->core_info.ideal_processor), cleanup);

    setAppCpuTimeLimitAndSchedModeFromDescriptor(localcaps->title_id, localcaps->reslimits[0]);

    if (launchFlags & PMLAUNCHFLAG_NORMAL_APPLICATION) {
        (*outProcessData)->flags |= PROCESSFLAG_NORMAL_APPLICATION; // not in official PM
    }

    if (launchFlags & PMLAUNCHFLAG_QUEUE_DEBUG_APPLICATION) {
        TRYG(svcDebugActiveProcess(outDebug, pid), cleanup);
    }

    cleanup:
    if (R_FAILED(res)) {
        svcTerminateProcess(processHandle); // missing in official pm in all code paths but one
        return res;
    }

    if (outProcessData != NULL) {
        *outProcessData = process;
    }

    return res;
}

static Result loadWithDependencies(Handle *outDebug, ProcessData **outProcessData, u64 programHandle, const FS_ProgramInfo *programInfo,
    u32 launchFlags, const ExHeader_Info *exheaderInfo)
{
    Result res = 0;

    struct {
        u64 titleId;
        u32 count;
        ProcessData *process;
    } dependenciesInfo[48] = {0};
    u32 totalNumDeps = 0, numNewDeps = 0, numDeps = 0;

    u64 currentDeps[48]; // note: official PM reuses exheader to save stack space but we have enough of it

    TRY(loadWithoutDependencies(outDebug, outProcessData, programHandle, programInfo, launchFlags, exheaderInfo));
    ProcessData *process = *outProcessData;


    listDependencies(currentDeps, &numDeps, exheaderInfo);
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
    if (depExheaderInfo == NULL) {
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
                if ((process->titleId & ~0xFFULL) == (dependenciesInfo[totalNumDeps + i].titleId & ~0xFFULL)) {
                    // Note: two processes can't have the same normalized titleId
                    dependenciesInfo[totalNumDeps + i].process = process;
                    if (process->flags & PROCESSFLAG_AUTOLOADED) {
                        if (process->refcount == 0xFF) {
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
        numNewDeps = 0;
        for (u32 i = 0; i < currentNumNewDeps; i++) {
            if (dependenciesInfo[totalNumDeps + i].process != NULL) {
                continue;
            }

            depProgramInfo.programId = dependenciesInfo[i].titleId;
            depProgramInfo.mediaType = MEDIATYPE_NAND;

            TRYG(launchTitleImpl(NULL, &process, &depProgramInfo, NULL, 0, depExheaderInfo), add_dup_refs);

            listDependencies(currentDeps, &numDeps, depExheaderInfo);
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

// Note: official PM has two distinct functions for sysmodule vs. regular app. We refactor that into a single function.
static Result launchTitleImpl(Handle *debug, ProcessData **outProcessData, const FS_ProgramInfo *programInfo,
    const FS_ProgramInfo *programInfoUpdate, u32 launchFlags, ExHeader_Info *exheaderInfo)
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
    res = R_SUCCEEDED(res) && exheaderInfo->aci.local_caps.core_info.core_version != SYSCOREVER ? (Result)0xC8A05800 : res;

    if (R_FAILED(res)) {
        LOADER_UnregisterProgram(programHandle);
        return res;
    }

    blacklistServices(exheaderInfo->aci.local_caps.title_id, exheaderInfo->aci.local_caps.service_access);

    if (launchFlags & PMLAUNCHFLAG_LOAD_DEPENDENCIES) {
        TRYG(loadWithDependencies(debug, outProcessData, programHandle, programInfo, launchFlags, exheaderInfo), cleanup);
        // note: official pm doesn't terminate the process if this fails (dependency loading)...
        // This may be intentional, but I believe this is a bug since the 0xD8A05805 and svcRun failure codepaths terminate the process...
        // It also forgets to clear PROCESSFLAG_NOTIFY_TERMINATION in the process...
    } else {
        TRYG(loadWithoutDependencies(debug, outProcessData, programHandle, programInfo, launchFlags, exheaderInfo), cleanup);
        // note: official pm doesn't terminate the proc. if it fails here either, but will because of the svcCloseHandle and the svcRun codepath
    }

    ProcessData *process = *outProcessData;
    if (launchFlags & PMLAUNCHFLAG_QUEUE_DEBUG_APPLICATION) {
        // saved field is different in official pm
        // this also means official pm can't launch a title with a debug flag and an application
        if (g_manager.debugData == NULL) {
            g_manager.debugData = process;
        } else {
            res = 0xD8A05805;
        }
    } else {
        si.priority = exheaderInfo->aci.local_caps.core_info.priority;
        si.stack_size = exheaderInfo->sci.codeset_info.stack_size;
        res = svcRun(process->handle, &si);
        if (R_SUCCEEDED(res) && (launchFlags & PMLAUNCHFLAG_NORMAL_APPLICATION) != 0) {
            g_manager.runningApplicationData = process;
            notifySubscribers(0x10C);
        }
    }

    cleanup:
    process = *outProcessData;
    if (process != NULL && R_FAILED(res)) {
        svcTerminateProcess(process->handle);
    } else if (process != NULL) {
        // official PM sets it but forgets to clear it on failure...
        process->flags = (launchFlags & PMLAUNCHFLAG_NOTIFY_TERMINATION) ? PROCESSFLAG_NOTIFY_TERMINATION : 0;
    }

    return res;
}

static Result launchTitleImplWrapper(Handle *outDebug, u32 *outPid, const FS_ProgramInfo *programInfo, const FS_ProgramInfo *programInfoUpdate, u32 launchFlags)
{
    ExHeader_Info *exheaderInfo = ExHeaderInfoHeap_New();
    if (exheaderInfo == NULL) {
        panic(0);
    }

    ProcessData *process;
    Result res = launchTitleImpl(outDebug, &process, programInfo, programInfoUpdate, launchFlags, exheaderInfo);

    if (outPid != NULL) {
        *outPid = process->pid;
    }

    ExHeaderInfoHeap_Delete(exheaderInfo);

    return res;
}

static void LaunchTitleAsync(void *argdata)
{
    struct {
        FS_ProgramInfo programInfo, programInfoUpdate;
        u32 launchFlags;
    } *args = argdata;

    launchTitleImplWrapper(NULL, NULL, &args->programInfo, &args->programInfoUpdate, args->launchFlags);
}

Result LaunchTitle(u32 *outPid, const FS_ProgramInfo *programInfo, u32 launchFlags)
{
    ProcessData *process, *foundProcess = NULL;

    launchFlags &= ~PMLAUNCHFLAG_USE_UPDATE_TITLE;

    if (g_manager.preparingForReboot) {
        return 0xC8A05801;
    }

    u32 tidh = (u32)(programInfo->programId >> 32);
    u32 tidl = (u32)programInfo->programId;
    if ((tidh == 0x00040030 || tidh == 0x00040130) && (tidl & 0xFF) != SYSCOREVER) {
        // Panic if launching SAFE_MODE sysmodules or applets (note: exheader syscorever check above only done for applications in official PM)
        // Official PM also hardcodes SYSCOREVER = 2 here.
        panic(4);
    }

    if ((g_manager.runningApplicationData != NULL || g_manager.debugData != NULL) && (launchFlags & PMLAUNCHFLAG_NORMAL_APPLICATION) != 0) {
        return 0xC8A05BF0;
    }

    ProcessList_Lock(&g_manager.processList);
    FOREACH_PROCESS(&g_manager.processList, process) {
        if ((process->titleId & ~0xFFULL) == (programInfo->programId & ~0xFFULL)) {
            foundProcess = process;
            break;
        }
    }
    ProcessList_Unlock(&g_manager.processList);

    if (foundProcess != NULL) {
        foundProcess->flags &= ~PROCESSFLAG_AUTOLOADED;
        if (outPid != NULL) {
            *outPid = foundProcess->pid;
        }
        return 0;
    } else {
        if (launchFlags & PMLAUNCHFLAG_QUEUE_DEBUG_APPLICATION || !(launchFlags & PMLAUNCHFLAG_NORMAL_APPLICATION)) {
            return launchTitleImplWrapper(NULL, outPid, programInfo, programInfo, launchFlags);
        } else {
            struct {
                FS_ProgramInfo programInfo, programInfoUpdate;
                u32 launchFlags;
            } args = { *programInfo, *programInfo, launchFlags };

            if (outPid != NULL) {
                *outPid = (u32)-1; // PM doesn't do that lol
            }
            TaskRunner_RunTask(LaunchTitleAsync, &args, sizeof(args));
            return 0;
        }
    }
}

Result LaunchTitleUpdate(const FS_ProgramInfo *programInfo, const FS_ProgramInfo *programInfoUpdate, u32 launchFlags)
{
    if (g_manager.preparingForReboot) {
        return 0xC8A05801;
    }
    if (g_manager.runningApplicationData != NULL || g_manager.debugData != NULL) {
        return 0xC8A05BF0;
    }
    if (!(launchFlags & ~PMLAUNCHFLAG_NORMAL_APPLICATION)) {
        return 0xD8E05802;
    }

    launchFlags |= PMLAUNCHFLAG_USE_UPDATE_TITLE;

    if (launchFlags & PMLAUNCHFLAG_QUEUE_DEBUG_APPLICATION) {
        return launchTitleImplWrapper(NULL, NULL, programInfo, programInfoUpdate, launchFlags);
    } else {
        struct {
            FS_ProgramInfo programInfo, programInfoUpdate;
            u32 launchFlags;
        } args = { *programInfo, *programInfoUpdate, launchFlags };

        TaskRunner_RunTask(LaunchTitleAsync, &args, sizeof(args));
        return 0;
    }
}

Result LaunchApp(const FS_ProgramInfo *programInfo, u32 launchFlags)
{
    if (g_manager.runningApplicationData != NULL || g_manager.debugData != NULL) {
        return 0xC8A05BF0;
    }

    assertSuccess(setAppCpuTimeLimit(0));
    return LaunchTitle(NULL, programInfo, launchFlags | PMLAUNCHFLAG_LOAD_DEPENDENCIES | PMLAUNCHFLAG_NORMAL_APPLICATION);
}

Result RunQueuedProcess(Handle *outDebug)
{
    Result res = 0;
    StartupInfo si = {0};

    if (g_manager.debugData == NULL) {
        return 0xD8A05804;
    } else if ((g_manager.debugData->flags & PROCESSFLAG_NORMAL_APPLICATION) && g_manager.runningApplicationData != NULL) {
        // Not in official PM
        return 0xC8A05BF0;
    }

    ProcessData *process = g_manager.debugData;
    g_manager.debugData = NULL;

    ExHeader_Info *exheaderInfo = ExHeaderInfoHeap_New();
    if (exheaderInfo == NULL) {
        panic(0);
    }

    TRYG(LOADER_GetProgramInfo(exheaderInfo, process->programHandle), cleanup);
    TRYG(svcDebugActiveProcess(outDebug, process->pid), cleanup);

    si.priority = exheaderInfo->aci.local_caps.core_info.priority;
    si.stack_size = exheaderInfo->sci.codeset_info.stack_size;
    res = svcRun(process->handle, &si);
    if (R_SUCCEEDED(res) && process->flags & PROCESSFLAG_NORMAL_APPLICATION) {
        // Second operand not in official PM
        g_manager.runningApplicationData = process;
        notifySubscribers(0x10C);
    }

    cleanup:
    if (R_FAILED(res)) {
        process->flags &= ~PROCESSFLAG_NOTIFY_TERMINATION;
        svcTerminateProcess(process->handle);
    }

    ExHeaderInfoHeap_Delete(exheaderInfo);

    return res;
}

Result LaunchAppDebug(Handle *outDebug, const FS_ProgramInfo *programInfo, u32 launchFlags)
{
    if (g_manager.debugData != NULL) {
        return RunQueuedProcess(outDebug);
    }

    if (g_manager.runningApplicationData != NULL) {
        return 0xC8A05BF0;
    }

    assertSuccess(setAppCpuTimeLimit(0));
    return launchTitleImplWrapper(outDebug, NULL, programInfo, programInfo,
        (launchFlags & ~PMLAUNCHFLAG_USE_UPDATE_TITLE) | PMLAUNCHFLAG_NORMAL_APPLICATION);
}

Result autolaunchSysmodules(void)
{
    Result res = 0;
    FS_ProgramInfo programInfo = { .mediaType = MEDIATYPE_NAND };

    // Launch NS
    if (NSTID != 0) {
        programInfo.programId = NSTID;
        TRY(launchTitleImplWrapper(NULL, NULL, &programInfo, &programInfo, PMLAUNCHFLAG_LOAD_DEPENDENCIES));
    }

    return res;
}
