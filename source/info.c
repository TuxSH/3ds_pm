#include <3ds.h>
#include <string.h>
#include "info.h"
#include "util.h"

Result listDependencies(u64 *dependencies, u32 *numDeps, ProcessData *process, ExHeader_Info *exheaderInfo, bool useLoader)
{
    Result res = 0;

    if (useLoader) {
        TRY(LOADER_GetProgramInfo(exheaderInfo, process->programHandle));
    }

    u32 num = 0;
    for (u32 i = 0; i < 48 && exheaderInfo->sci.dependencies[i] != 0; i++) {
        u64 titleId = exheaderInfo->sci.dependencies[i];
        if (IS_N3DS || (titleId & 0xF0000000) == 0) {
            // On O3DS, ignore N3DS titles.
            // Then (on both) remove the N3DS titleId bits
            dependencies[num++] = titleId & ~0xF0000000;
        }
    }

    *numDeps = num;
    return res;
}
