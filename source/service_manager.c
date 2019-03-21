#include <3ds.h>
#include "service_manager.h"

#define TRY(expr) if(R_FAILED(res = (expr))) goto cleanup;

Result ServiceManager_Run(const ServiceManagerServiceEntry *services, const ServiceManagerNotificationEntry *notifications, const ServiceManagerContextAllocator *allocator)
{
    Result res = 0;

    u32 numServices = 0;
    u32 maxSessionsTotal = 0;
    u32 numActiveSessions = 0;
    bool terminationRequested = false;

    for (u32 i = 0; services[i].name != NULL; i++) {
        numServices++;
        maxSessionsTotal += services[i].maxSessions;
    }

    Handle waitHandles[1 + numServices + maxSessionsTotal];
    void *ctxs[maxSessionsTotal];
    u8 handlerIds[maxSessionsTotal];

    Handle replyTarget = 0;
    s32 id = -1;
    u32 *cmdbuf = getThreadCommandBuffer();

    TRY(srvEnableNotification(&waitHandles[0]));

    for (u32 i = 0; i < numServices; i++) {
        if (!services[i].isGlobalPort) {
            TRY(srvRegisterService(&waitHandles[1 + i], services[i].name, (s32)services[i].maxSessions));
        } else {
            Handle clientPort;
            TRY(svcCreatePort(&waitHandles[1 + i], &clientPort, services[i].name, (s32)services[i].maxSessions));
            svcCloseHandle(clientPort);
        }
    }

    while (!terminationRequested) {
        if (replyTarget == 0) {
            cmdbuf[0] = 0xFFFF0000;
        }

        id = -1;
        res = svcReplyAndReceive(&id, waitHandles, 1 + numServices + numActiveSessions, replyTarget);

        if (res == (Result)0xC920181A) {
            // Session has been closed
            u32 off;
            if (id == -1) {
                for (off = 0; off < numActiveSessions && waitHandles[1 + numServices + off] != replyTarget; off++);
                if (off >= numActiveSessions) {
                    return res;
                }

                id = 1 + numServices + off;
            } else if ((u32)id < 1 + numServices) {
                return res;
            }

            off = id - 1 - numServices;

            Handle h = waitHandles[id];
            void *ctx = ctxs[off];
            waitHandles[id] = waitHandles[1 + numServices + --numActiveSessions];
            handlerIds[off] = handlerIds[numActiveSessions];
            ctxs[off] = ctxs[numActiveSessions];

            svcCloseHandle(h);
            if (allocator != NULL) {
                allocator->freeSessionContext(ctx);
            }

            replyTarget = 0;
            res = 0;
        } else if (R_FAILED(res)) {
            return res;
        }

        else {
            // Ok, no session closed and no error
            replyTarget = 0;
            if (id == 0) {
                // Notification
                u32 notificationId = 0;
                TRY(srvReceiveNotification(&notificationId));
                terminationRequested = notificationId == 0x100;

                for (u32 i = 0; notifications[i].handler != NULL; i++) {
                    if (notifications[i].id == notificationId) {
                        notifications[i].handler(notificationId);
                        break;
                    }
                }
            } else if ((u32)id < 1 + numServices) {
                // New session
                Handle session;
                void *ctx = NULL;
                TRY(svcAcceptSession(&session, waitHandles[id]));

                if (allocator) {
                    ctx = allocator->newSessionContext((u8)(id - 1));
                    if (ctx == NULL) {
                        svcCloseHandle(session);
                        return 0xDEAD0000;
                    }
                }

                waitHandles[1 + numServices + numActiveSessions] = session;
                handlerIds[numActiveSessions] = (u8)(id - 1);
                ctxs[numActiveSessions++] = ctx;
            } else {
                // Service command
                u32 off = id - 1 - numServices;
                services[handlerIds[off]].handler(ctxs[off]);
                replyTarget = waitHandles[id];
            }
        }
    }

cleanup:
    for (u32 i = 0; i < 1 + numServices + numActiveSessions; i++) {
        svcCloseHandle(waitHandles[i]);
    }

    for (u32 i = 0; i < numServices; i++) {
        if (!services[i].isGlobalPort) {
            srvUnregisterService(services[i].name);
        }
    }

    if (allocator) {
        for (u32 i = 0; i < numActiveSessions; i++) {
            allocator->freeSessionContext(ctxs[i]);
        }
    }

    return res;
}
