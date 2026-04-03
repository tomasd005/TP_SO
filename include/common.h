#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

#define CONTROLLER_FIFO " /tmp/controller_fifo"

typedef struct
{
    int type;
    pid_t pid;
    int user_id;
    char fifo_name[64];
    char command[256];
} Request

#endif
