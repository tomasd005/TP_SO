#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <common.h>

/* Constantes locais do controller*/

#define MAX_FIFO    64
#define MAX_COMMAND 256
#define LOG_FILE    "tmp/log.txt"
#define MAX_QUEUE   256

typedef struct {
    int active;
    pid_t pid;
    int user_id;
    char fifo_name[MAX_FIFO];
    char command[MAX_COMMAND];
    struct timeval submitted_at;
} Entry;

static Entry queue[MAX_QUEUE];
static int   q_head = 0;
static int   q_tail = 0;
static int   q_size = 0;

static Entry *running    = NULL;
static int max_slots  = 1;
static int used_slots = 0;

static int  shutdown_pending = 0;
static char shutdown_fifo[MAX_FIFO] = {0};

/* Política de escalonamento: "rr" (Round-Robin, default) ou "fifo" */
static char sched_policy[16] = "rr";

/*Utilitários de fila*/

static int queue_push(const Request *req, struct timeval *tv)
{
    if (q_size >= MAX_QUEUE) return -1;
    Entry *e = &queue[q_tail];
    e->active = 1;
    e->pid = req->pid;
    e->user_id  = req->user_id;
    strncpy(e->fifo_name, req->fifo_name, MAX_FIFO - 1);
    strncpy(e->command,   req->command,   MAX_COMMAND - 1);
    e->submitted_at = *tv;
    q_tail = (q_tail + 1) % MAX_QUEUE;
    q_size++;
    return 0;
}

static void queue_remove_at(int k, Entry *out)
{
    int idx = (q_head + k) % MAX_QUEUE;
    *out = queue[idx];
    for (int i = k; i < q_size - 1; i++) {
        int cur  = (q_head + i)     % MAX_QUEUE;
        int next = (q_head + i + 1) % MAX_QUEUE;
        queue[cur] = queue[next];
    }
    int last = (q_head + q_size - 1) % MAX_QUEUE;
    queue[last].active = 0;
    q_tail = (q_tail - 1 + MAX_QUEUE) % MAX_QUEUE;
    q_size--;
}

/*Política de escalonamento*/

/* FIFO: despacha sempre o primeiro da fila */
static int schedule_next_fifo(void)
{
    if (q_size == 0) return -1;
    return 0;
}

/* Round-Robin: prefere utilizador que não esteja já a correr */
static int schedule_next_rr(void)
{
    if (q_size == 0) return -1;

    int running_users[MAX_QUEUE];
    int nr = 0;
    for (int i = 0; i < max_slots; i++)
        if (running[i].active)
            running_users[nr++] = running[i].user_id;

    /* Preferir utilizador que não esteja já a correr */
    for (int k = 0; k < q_size; k++) {
        int idx = (q_head + k) % MAX_QUEUE;
        int uid = queue[idx].user_id;
        int found = 0;
        for (int j = 0; j < nr; j++)
            if (running_users[j] == uid) { found = 1; break; }
        if (!found) return k;
    }

    /* Todos os users em fila já têm algo a correr → FIFO */
    return 0;
}

static int schedule_next_index(void)
{
    if (strcmp(sched_policy, "fifo") == 0)
        return schedule_next_fifo();
    return schedule_next_rr();
}

/*Despachar comandos da fila para slots livres*/

static void try_dispatch(void)
{
    while (used_slots < max_slots && q_size > 0) {
        int k = schedule_next_index();
        if (k < 0) break;

        int slot = -1;
        for (int i = 0; i < max_slots; i++)
            if (!running[i].active) { slot = i; break; }
        if (slot < 0) break;

        Entry e;
        queue_remove_at(k, &e);
        running[slot] = e;
        running[slot].active = 1;
        used_slots++;

        /* Autorizar o runner */
        int fd = open(e.fifo_name, O_WRONLY);
        if (fd != -1) {
            char ok = '1';
            write(fd, &ok, 1);
            close(fd);
        }
    }
}

/*Log persistente*/

static void log_finished(const Entry *e, double dur)
{
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) return;
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "user=%d pid=%d duration=%.3fs cmd=%s\n",
        e->user_id, e->pid, dur, e->command);
    write(fd, buf, n);
    close(fd);
}

/*Resposta ao pedido STATUS (-c)*/

static void handle_status(const Request *req)
{
    int fd = open(req->fifo_name, O_WRONLY);
    if (fd == -1) return;

    char buf[4096];
    int  pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nExecuting\n");
    for (int i = 0; i < max_slots; i++) {
        if (running[i].active)
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                "user-id %d - command-id %d\n",
                running[i].user_id, running[i].pid);
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "---\nScheduled\n");
    for (int k = 0; k < q_size; k++) {
        int idx = (q_head + k) % MAX_QUEUE;
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "user-id %d - command-id %d\n",
            queue[idx].user_id, queue[idx].pid);
    }

    write(fd, buf, pos);
    close(fd);
}

/*main*/

int main(int argc, char *argv[])
{
    if (argc >= 2) {
        max_slots = atoi(argv[1]);
        if (max_slots < 1) max_slots = 1;
    }
    if (argc >= 3) {
        strncpy(sched_policy, argv[2], sizeof(sched_policy) - 1);
        sched_policy[sizeof(sched_policy) - 1] = '\0';
    }

    running = calloc(max_slots, sizeof(Entry));
    if (!running) {
        perror("calloc");
        return 1;
    }

    if (mkfifo(CONTROLLER_FIFO, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo controller");
        free(running);
        return 1;
    }

    /*
     * O_RDWR garante que o FIFO nunca entrega EOF ao controller
     * enquanto não há writers — evita busy-loop no read().
     */
    int fd_ctrl = open(CONTROLLER_FIFO, O_RDWR);
    if (fd_ctrl == -1) {
        perror("open controller fifo");
        free(running);
        return 1;
    }

    char msg[64];
    int  n = snprintf(msg, sizeof(msg), "[controller] started (slots=%d policy=%s)\n", max_slots, sched_policy);
    write(STDOUT_FILENO, msg, n);

    for (;;) {
        Request req;
        ssize_t r = read(fd_ctrl, &req, sizeof(Request));
        if (r <= 0) continue;

        struct timeval now;
        gettimeofday(&now, NULL);

        switch (req.type) {

        case EXEC:
            queue_push(&req, &now);
            try_dispatch();
            break;

        case FINISHED:
            for (int i = 0; i < max_slots; i++) {
                if (running[i].active && running[i].pid == req.pid) {
                    double dur =
                        (now.tv_sec  - running[i].submitted_at.tv_sec) +
                        (now.tv_usec - running[i].submitted_at.tv_usec) / 1e6;
                    log_finished(&running[i], dur);
                    running[i].active = 0;
                    used_slots--;
                    break;
                }
            }
            try_dispatch();

            if (shutdown_pending && used_slots == 0 && q_size == 0) {
                int fd = open(shutdown_fifo, O_WRONLY);
                if (fd != -1) { char ok = '1'; write(fd, &ok, 1); close(fd); }
                goto cleanup;
            }
            break;

        case STATUS:
            handle_status(&req);
            break;

        case SHUTDOWN:
            shutdown_pending = 1;
            strncpy(shutdown_fifo, req.fifo_name, MAX_FIFO - 1);
            if (used_slots == 0 && q_size == 0) {
                int fd = open(shutdown_fifo, O_WRONLY);
                if (fd != -1) { char ok = '1'; write(fd, &ok, 1); close(fd); }
                goto cleanup;
            }
            break;
        }
    }

cleanup:
    close(fd_ctrl);
    unlink(CONTROLLER_FIFO);
    free(running);
    n = snprintf(msg, sizeof(msg), "[controller] shutdown complete\n");
    write(STDOUT_FILENO, msg, n);
    return 0;
}