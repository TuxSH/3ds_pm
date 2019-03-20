#pragma once

#include <3ds/exheader.h>
#include <3ds/services/fs.h>
#include "process_data.h"

// Note: official PM has two distinct functions for sysmodule vs. regular app. We refactor that into a single function.
Result launchTitleImpl(Handle *debug, ProcessData **outProcessData, const FS_ProgramInfo *programInfo,
    const FS_ProgramInfo *programInfoUpdate, u32 launchFlags, const ExHeader_Info *exheaderInfo);
