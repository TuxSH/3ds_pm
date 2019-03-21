#include <3ds.h>
#include <string.h>
#include "launch.h"
#include "util.h"

void pmDbgHandleCommands(void *ctx)
{
    (void)ctx;
    u32 *cmdbuf = getThreadCommandBuffer();
    u32 cmdhdr = cmdbuf[0];

    FS_ProgramInfo programInfo;
    Handle debug;

    switch (cmdhdr >> 16) {
        case 1:
            debug = 0;
            memcpy(&programInfo, cmdbuf + 1, sizeof(FS_ProgramInfo));
            cmdbuf[1] = LaunchAppDebug(&debug, &programInfo, cmdbuf[5]);
            cmdbuf[0] = IPC_MakeHeader(1, 1, 2);
            cmdbuf[2] = IPC_Desc_MoveHandles(1);
            cmdbuf[3] = debug;
            break;
        case 2:
            memcpy(&programInfo, cmdbuf + 1, sizeof(FS_ProgramInfo));
            cmdbuf[1] = LaunchApp(&programInfo, cmdbuf[5]);
            cmdbuf[0] = IPC_MakeHeader(2, 1, 0);
            break;
        case 3:
            debug = 0;
            cmdbuf[1] = RunQueuedProcess(&debug);
            cmdbuf[0] = IPC_MakeHeader(3, 1, 2);
            cmdbuf[2] = IPC_Desc_MoveHandles(1);
            cmdbuf[3] = debug;
            break;
        default:
            cmdbuf[0] = IPC_MakeHeader(0, 1, 0);
            cmdbuf[1] = 0xD900182F;
            break;
    }
}
