#include <3ds.h>

#include "manager.h"

// this is called before main
bool isN3DS;
void __appInit()
{
    srvPmInit();
    loaderInit();
    fsRegInit();
}

// this is called after main exits
void __appExit()
{
    fsRegExit();
    loaderExit();
    srvPmExit();
}


Result __sync_init(void);
Result __sync_fini(void);
void __libc_init_array(void);
void __libc_fini_array(void);

void __ctru_exit()
{
    __libc_fini_array();
    __appExit();
    __sync_fini();
    svcExitProcess();
}

void initSystem()
{
    __sync_init();
    __appInit();
    __libc_init_array();
}

int main(void)
{
    return 0;
}
