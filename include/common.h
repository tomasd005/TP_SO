
#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

#define CONTROLLER_FIFO "/tmp/controller_fifo" // endereço publico do controller

typedef enum
{
    EXEC = 1,
    STATUS = 2,
    SHUTDOWN = 3,
    FINISHED = 4
} RequestType;

typedef struct
{
    RequestType type;
    pid_t pid;
    int user_id;
    char fifo_name[64];
    char command[256];
} Request;

#endif