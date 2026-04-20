#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
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
    if (strcmp(argv[1], "-e") == 0) // moddo executar
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
        // 1. enviar request ao controller

        int fd_controller = open(CONTROLLER_FIFO, O_WRONLY);
        if (fd_controller == -1)
        {
            perror("open controller fifo");
            unlink(fifo_name);
            return 1;
        }
        write(fd_controller, &req, sizeof(Request));
        close(fd_controller);

        // notificar utilizador

        char msg[128];
        int len = snprintf(msg, sizeof(msg), "[runner] command %d submitted\n", pid);
        write(STDOUT_FILENO, msg, len);

        // 2. esperar resposta do controller no fifo privado
        int fd_private = open(fifo_name, O_RDONLY);
        if (fd_private == -1)
        {
            perror("open private fifo");
            unlink(fifo_name);
            return 1;
        }

        char response;
        read(fd_private, &response, 1);
        close(fd_private);

        len = snprintf(msg, sizeof(msg), "[runner] executing command %d...\n", pid);
        write(STDOUT_FILENO, msg, len);

        // 3. executar o comando com fork + exec
        
        pid_t child = fork();
        if (child == -1)
        {
            perror("fork");
            unlink(fifo_name);
            return 1;
        }

        if (child == 0)
        {
            // processo filho: executar o comando
            execlp("/bin/sh", "sh", "-c", req.command, NULL);
            perror("execlp");
            _exit(1);
        }

        // processo pai: esperar o filho terminar
        int status;
        waitpid(child, &status, 0);

        // 4. notificar controller que o comando terminou
        Request done;
        done.type = FINISHED;
        done.pid = pid;
        done.user_id = req.user_id;
        strcpy(done.fifo_name, fifo_name);
        strcpy(done.command, req.command);

        fd_controller = open(CONTROLLER_FIFO, O_WRONLY);
        if (fd_controller != -1)
        {
            write(fd_controller, &done, sizeof(Request));
            close(fd_controller);
        }

        len = snprintf(msg, sizeof(msg), "[runner] command %d finished\n", pid);
        write(STDOUT_FILENO, msg, len);

        // 5. limpar fifo privado
        unlink(fifo_name);
        return 0;
    }

    if (strcmp(argv[1], "-c") == 0) // modo consulta
    {
        if (argc != 2)
        {
            print_usage();
            return 1;
        }

        pid_t pid = getpid();
        char fifo_name[64];
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/runner_%d", pid);

        if (mkfifo(fifo_name, 0666) == -1 && errno != EEXIST)
        {
            perror("mkfifo");
            return 1;
        }

        // construir pedido de consulta
        Request req;
        req.type = STATUS;
        req.pid = pid;
        req.user_id = 0;
        strcpy(req.fifo_name, fifo_name);
        req.command[0] = '\0';

        // enviar pedido ao controller
        int fd_controller = open(CONTROLLER_FIFO, O_WRONLY);
        if (fd_controller == -1)
        {
            perror("open controller fifo");
            unlink(fifo_name);
            return 1;
        }
        write(fd_controller, &req, sizeof(Request));
        close(fd_controller);

        // ler resposta do controller no fifo privado
        int fd_private = open(fifo_name, O_RDONLY);
        if (fd_private == -1)
        {
            perror("open private fifo");
            unlink(fifo_name);
            return 1;
        }

        char buf[4096];
        ssize_t n;
        while ((n = read(fd_private, buf, sizeof(buf))) > 0)
        {
            write(STDOUT_FILENO, buf, n);
        }
        close(fd_private);

        unlink(fifo_name);
        return 0;
    }
    if (strcmp(argv[1], "-s") == 0) // modo shutdown
    {
        if (argc != 2)
        {
            print_usage();
            return 1;
        }

        pid_t pid = getpid();
        char fifo_name[64];
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/runner_%d", pid);

        if (mkfifo(fifo_name, 0666) == -1 && errno != EEXIST)
        {
            perror("mkfifo");
            return 1;
        }

        // construir pedido de shutdown
        Request req;
        req.type = SHUTDOWN;
        req.pid = pid;
        req.user_id = 0;
        strcpy(req.fifo_name, fifo_name);
        req.command[0] = '\0';

        // enviar pedido ao controller
        int fd_controller = open(CONTROLLER_FIFO, O_WRONLY);
        if (fd_controller == -1)
        {
            perror("open controller fifo");
            unlink(fifo_name);
            return 1;
        }
        write(fd_controller, &req, sizeof(Request));
        close(fd_controller);

        char msg[128];
        int len = snprintf(msg, sizeof(msg), "[runner] sent shutdown notification\n");
        write(STDOUT_FILENO, msg, len);

        // esperar que o controller confirme o shutdown
        len = snprintf(msg, sizeof(msg), "[runner] waiting for controller to shutdown...\n");
        write(STDOUT_FILENO, msg, len);

        int fd_private = open(fifo_name, O_RDONLY);
        if (fd_private == -1)
        {
            perror("open private fifo");
            unlink(fifo_name);
            return 1;
        }

        char response;
        read(fd_private, &response, 1);
        close(fd_private);

        len = snprintf(msg, sizeof(msg), "[runner] controller exited.\n");
        write(STDOUT_FILENO, msg, len);

        unlink(fifo_name);
        return 0;
    }

    print_usage();
    return 1;
}
