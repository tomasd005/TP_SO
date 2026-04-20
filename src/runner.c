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
    printf("./runner -e <user-id> \"<command> <arg1> <arg2> <...>\"\n");
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
        if (argc != 4)
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
        strcpy(req.command, argv[3]);
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
            // processo filho: parsing do comando com suporte a pipes e redirects
            char cmd_copy[256];
            strncpy(cmd_copy, req.command, sizeof(cmd_copy));
            cmd_copy[sizeof(cmd_copy) - 1] = '\0';

            // tokenizar o comando inteiro
            char *tokens[64];
            int ntokens = 0;
            char *tok = strtok(cmd_copy, " ");
            while (tok != NULL && ntokens < 63)
            {
                tokens[ntokens++] = tok;
                tok = strtok(NULL, " ");
            }

            if (ntokens == 0)
                _exit(1);

            // dividir em segmentos separados por '|'
            // cada segmento e um comando do pipeline
            int seg_start[16]; // indice inicial de cada segmento no array tokens
            int seg_len[16];   // numero de tokens em cada segmento
            int nsegs = 0;

            seg_start[0] = 0;
            int cur_len = 0;
            for (int i = 0; i < ntokens; i++)
            {
                if (strcmp(tokens[i], "|") == 0)
                {
                    seg_len[nsegs] = cur_len;
                    nsegs++;
                    seg_start[nsegs] = i + 1;
                    cur_len = 0;
                }
                else
                {
                    cur_len++;
                }
            }
            seg_len[nsegs] = cur_len;
            nsegs++;

            // executar o pipeline
            int prev_read_fd = -1; // fd de leitura do pipe anterior

            for (int s = 0; s < nsegs; s++)
            {
                // criar pipe para ligar ao proximo segmento (se nao for o ultimo)
                int pipefd[2] = {-1, -1};
                if (s < nsegs - 1)
                {
                    if (pipe(pipefd) == -1)
                    {
                        perror("pipe");
                        _exit(1);
                    }
                }

                // extrair argumentos e redirects deste segmento
                char *args[64];
                char *file_in = NULL, *file_out = NULL, *file_err = NULL;
                int nargs = 0;

                for (int i = seg_start[s]; i < seg_start[s] + seg_len[s]; i++)
                {
                    if (strcmp(tokens[i], "<") == 0 && i + 1 < seg_start[s] + seg_len[s])
                    {
                        file_in = tokens[++i];
                    }
                    else if (strcmp(tokens[i], ">") == 0 && i + 1 < seg_start[s] + seg_len[s])
                    {
                        file_out = tokens[++i];
                    }
                    else if (strcmp(tokens[i], "2>") == 0 && i + 1 < seg_start[s] + seg_len[s])
                    {
                        file_err = tokens[++i];
                    }
                    else
                    {
                        args[nargs++] = tokens[i];
                    }
                }
                args[nargs] = NULL;

                if (nargs == 0)
                    _exit(1);

                pid_t seg_pid = fork();
                if (seg_pid == -1)
                {
                    perror("fork");
                    _exit(1);
                }

                if (seg_pid == 0)
                {
                    // stdin: vem do pipe anterior (se existir)
                    if (prev_read_fd != -1)
                    {
                        dup2(prev_read_fd, STDIN_FILENO);
                        close(prev_read_fd);
                    }

                    // stdout: vai para o proximo pipe (se existir)
                    if (pipefd[1] != -1)
                    {
                        dup2(pipefd[1], STDOUT_FILENO);
                        close(pipefd[1]);
                    }
                    if (pipefd[0] != -1)
                        close(pipefd[0]);

                    // redirect de input (<)
                    if (file_in)
                    {
                        int fd = open(file_in, O_RDONLY);
                        if (fd == -1) { perror("open"); _exit(1); }
                        dup2(fd, STDIN_FILENO);
                        close(fd);
                    }

                    // redirect de output (>)
                    if (file_out)
                    {
                        int fd = open(file_out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd == -1) { perror("open"); _exit(1); }
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }

                    // redirect de stderr (2>)
                    if (file_err)
                    {
                        int fd = open(file_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd == -1) { perror("open"); _exit(1); }
                        dup2(fd, STDERR_FILENO);
                        close(fd);
                    }

                    execvp(args[0], args);
                    perror("execvp");
                    _exit(1);
                }

                // processo coordenador: fechar fds que ja nao precisa
                if (prev_read_fd != -1)
                    close(prev_read_fd);
                if (pipefd[1] != -1)
                    close(pipefd[1]);
                prev_read_fd = pipefd[0];
            }

            // esperar que todos os segmentos do pipeline terminem
            int seg_status;
            for (int s = 0; s < nsegs; s++)
                wait(&seg_status);

            _exit(WIFEXITED(seg_status) ? WEXITSTATUS(seg_status) : 1);
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
