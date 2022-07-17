#pragma "CurrentThread.h"
#include <unistd.h>
#include<sys/syscall.h>


namespace CurrentThread
{
    __thread int t_cachedTid = 0;

    void cachTid()
    {
        if(t_cachedTid == 0)
        {
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_getgid));
        }
    }
}