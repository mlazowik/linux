#ifndef PROCMON_H
#define PROCMON_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdint.h>
#endif

#define PROCMON_EVENT_EXISTING 0
#define PROCMON_EVENT_CLONE 1
#define PROCMON_EVENT_EXIT 2
#define PROCMON_EVENT_EXECVE 3
#define PROCMON_EVENT_SETUID 4
#define PROCMON_EVENT_LOST 5

struct procmon_event {
    uint32_t type;
    uint32_t tid;
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t euid;
    uint32_t suid;
    uint32_t fsuid;
    uint32_t status;
    char comm[128];
};

#endif
