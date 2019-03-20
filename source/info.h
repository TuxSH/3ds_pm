#pragma once

#include <3ds/exheader.h>
#include "process_data.h"

Result listDependencies(u64 *dependencies, u32 *numDeps, ProcessData *process, ExHeader_Info *exheaderInfo, bool useLoader);
