#include <3ds.h>
#include <string.h>
#include "exheader_info_heap.h"
#include "manager.h"
#include "info.h"
#include "util.h"

Result getAndListDependencies(u64 *dependencies, u32 *numDeps, ProcessData *process, ExHeader_Info *exheaderInfo)
{
    Result res = 0;
    TRY(LOADER_GetProgramInfo(exheaderInfo, process->programHandle));
    return listDependencies(dependencies, numDeps, exheaderInfo);
}

Result listDependencies(u64 *dependencies, u32 *numDeps, const ExHeader_Info *exheaderInfo)
{
    Result res = 0;

    u32 num = 0;
    for (u32 i = 0; i < 48 && exheaderInfo->sci.dependencies[i] != 0; i++) {
        u64 titleId = exheaderInfo->sci.dependencies[i];
        if (IS_N3DS || (titleId & N3DS_TID_MASK) == 0) {
            // On O3DS, ignore N3DS titles.
            // Then (on both) remove the N3DS titleId bits
            dependencies[num++] = titleId & ~N3DS_TID_MASK;
        }
    }

    *numDeps = num;
    return res;
}

Result listMergeUniqueDependencies(ProcessData **procs, u64 *dependencies, u32 *remrefcounts, u32 *numDeps, const ExHeader_Info *exheaderInfo)
{
    Result res = 0;
    u32 numDepsUnique = *numDeps;
    u32 num2;

    u64 deps[48];
    u32 newrefcounts[48] = {0};

    TRY(listDependencies(deps, &num2, exheaderInfo));

    ProcessList_Lock(&g_manager.processList);
    for (u32 i = 0; i < num2; i++) {
        // Filter duplicate results
        u32 j;
        for (j = 0; j < numDepsUnique && deps[i] != dependencies[j]; j++);
        if (j >= numDepsUnique) {
            if (numDepsUnique >= 48) {
                panic(2);
            }
            dependencies[numDepsUnique] = deps[i];
            newrefcounts[numDepsUnique] = 1;
            procs[numDepsUnique] = ProcessList_FindProcessByTitleId(&g_manager.processList, deps[i]);
            numDepsUnique++;
        } else {
            ++newrefcounts[j];
        }
    }

    // Apply refcounts
    for (u32 i = 0; i < numDepsUnique; i++) {
        if (procs[i] != NULL) {
            ProcessData_Incref(procs[i], newrefcounts[i]);
        } else {
            remrefcounts[i] += newrefcounts[i];
        }
    }
    ProcessList_Unlock(&g_manager.processList);

    *numDeps = numDepsUnique;
    return res;
}


Result GetTitleExHeaderFlags(ExHeader_Arm11CoreInfo *outCoreInfo, ExHeader_SystemInfoFlags *outSiFlags, const FS_ProgramInfo *programInfo)
{
    Result res = 0;
    u64 programHandle = 0;

    if (g_manager.preparingForReboot) {
        return 0xC8A05801;
    }

    ExHeader_Info *exheaderInfo = ExHeaderInfoHeap_New();
    if (exheaderInfo == NULL) {
        panic(0);
    }

    res = LOADER_RegisterProgram(&programHandle, programInfo->programId, programInfo->mediaType,
    programInfo->programId, programInfo->mediaType);

    if (R_SUCCEEDED(res))
    {
        res = LOADER_GetProgramInfo(exheaderInfo, programHandle);
        if (R_SUCCEEDED(res)) {
            *outCoreInfo = exheaderInfo->aci.local_caps.core_info;
            *outSiFlags = exheaderInfo->sci.codeset_info.flags;
        }
        LOADER_UnregisterProgram(programHandle);
    }

    ExHeaderInfoHeap_Delete(exheaderInfo);

    return res;
}
