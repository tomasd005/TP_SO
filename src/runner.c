#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include <stdlib.h>

void print_usage(void)
{
    printf("usage:\n");
    printf("./runner -e <user-id> <command> [args...]\n");
    printf("./runner -c\n");
    printf("./runner -s\n");
}
int main(int argc, char *argv[])
{
    if (argc < 2) // caso de uma entrada do tipo "./runner", logo nao e valido
    {
        print_usage();
        return 1;
    }
    if (strcmp(argv[1], "-e") == 0)
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        pid_t pid = getpid();
        char fifo_name[64];
        Request req; // fifo privado criado

        snprintf(fifo_name, sizeof(fifo_name), "/tmp/runner_%d",
                 pid);

        if (mkfifo(fifo_name, 0666) == -1 && errno != EEXIST)
        {
            perror("mkfifo");
            return 1;
        }

        req.type = EXEC;
        req.pid = pid;
        req.user_id = atoi(argv[2]);
        strcpy(req.fifo_name, fifo_name);
        req.command[0] = '\0';
        for (int i = 3; i < argc; i++)
        {
            strcat(req.command, argv[i]);
            if (i < argc - 1)
            {
                strcat(req.command, " ");
            }
        }
        printf("type: %d\n", req.type);
        printf("pid: %d\n", req.pid);
        printf("user_id: %d\n", req.user_id);
        printf("fifo_name: %s\n", req.fifo_name);
        printf("command: %s\n", req.command);

        return 0;
    }

    if (strcmp(argv[1], "-c") == 0)
    {
        if (argc != 2)
        {
            print_usage();
            return 1;
        }
        printf("modo consulta\n");
        return 0;
    }
    if (strcmp(argv[1], "-s") == 0)
    {
        if (argc != 2)
        {
            print_usage();
            return 1;
        }

        printf("modo shutdown\n");
        return 0;
    }

    print_usage();
    return 1;
}
