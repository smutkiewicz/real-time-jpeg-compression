#include "edf.h"

namespace EDF
{
    int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags)
    {
        return syscall(__NR_sched_setattr, pid, attr, flags);
    }

    int sched_getattr(pid_t pid, struct sched_attr *attr, unsigned int size, unsigned int flags)
    {
        return syscall(__NR_sched_getattr, pid, attr, size, flags);
    }

    void *run_deadline(void *data)
    {
        struct sched_attr attr{};
        int x = 0, ret;
        unsigned int flags = 0;

        printf("deadline thread start %ld\n", gettid());

        attr.size = sizeof(attr);
        attr.sched_flags = 0;
        attr.sched_nice = 0;
        attr.sched_priority = 0;

        /* creates a 10ms/30ms reservation */
        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_runtime = 10 * 1000 * 1000;
        attr.sched_period = 30 * 1000 * 1000;
        attr.sched_deadline = 30 * 1000 * 1000;

        ret = sched_setattr(0, &attr, flags);
        if (ret < 0)
        {
            done = 0;
            perror("sched_setattr");
            exit(-1);
        }

        while (!done)
        {
            x++;
        }
        return nullptr;
    }
}