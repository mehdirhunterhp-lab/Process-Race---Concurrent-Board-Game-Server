#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

#define PORT 8080
#define MAX_PLAYERS 3
#define WIN_SCORE 20

/* shared game state */
typedef struct {
    pthread_mutex_t mutex;
    int scores[MAX_PLAYERS];
    int current_turn;
    int players_connected;
    int game_over;
    int turn_completed;
} GameState;

static GameState *shm = NULL;
static int log_pipe[2];

/* logging helper */
static void send_log(const char *msg)
{
    char buf[256];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    snprintf(buf, sizeof(buf),
             "[%02d:%02d:%02d] %s\n",
             tm_now->tm_hour,
             tm_now->tm_min,
             tm_now->tm_sec,
             msg);

    write(log_pipe[1], buf, strlen(buf));
}

/* load previous scores */
static void load_scores(void)
{
    FILE *f = fopen("scores.txt", "r");
    if (!f) {
        send_log("SYSTEM: No previous score file found");
        return;
    }

    fclose(f);
    send_log("SYSTEM: Found old score file (values ignored)");
}

/* save scores to disk */
static void save_scores(void)
{
    FILE *f = fopen("scores.txt", "w");
    if (!f)
        return;

    for (int i = 0; i < MAX_PLAYERS; i++)
        fprintf(f, "Player %d: %d\n", i, shm->scores[i]);

    fclose(f);
    send_log("SYSTEM: Scores written to disk");
}

/* logger thread */
static void *logger_thread(void *arg)
{
    FILE *f = fopen("game.log", "a");
    if (!f)
        return NULL;

    char buf[256];
    while (1) {
        ssize_t n = read(log_pipe[0], buf, sizeof(buf) - 1);
        if (n <= 0)
            break;

        buf[n] = '\0';
        fputs(buf, f);
        fflush(f);
        fputs(buf, stdout);
    }

    fclose(f);
    return NULL;
}

/* turn scheduler thread */
static void *scheduler_thread(void *arg)
{
    send_log("scheduler online");

    for (;;) {
        if (shm->game_over)
            break;

        if (shm->players_connected < MAX_PLAYERS) {
            usleep(150000);
            continue;
        }

        pthread_mutex_lock(&shm->mutex);

        if (shm->game_over) {
            pthread_mutex_unlock(&shm->mutex);
            break;
        }

        if (shm->turn_completed) {
            int old = shm->current_turn;
            shm->current_turn = (shm->current_turn + 1) % MAX_PLAYERS;
            shm->turn_completed = 0;

            char m[96];
            snprintf(m, sizeof(m), "scheduler: %d -> %d", old, shm->current_turn);
            send_log(m);
        }

        pthread_mutex_unlock(&shm->mutex);
        usleep(100000);
    }

    return NULL;
}

/* zombie process reaper */
static void reap_children(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

/* client handler process */
static void handle_client(int id, int sock)
{
    close(log_pipe[0]);

    char buf[1024];
    char msg[1024];

    snprintf(msg, sizeof(msg),
             "Welcome Player %d. Waiting for others...\n", id);
    send(sock, msg, strlen(msg), 0);

    while (1) {
        int my_turn = 0;

        while (!my_turn) {
            pthread_mutex_lock(&shm->mutex);

            if (shm->game_over) {
                pthread_mutex_unlock(&shm->mutex);
                send(sock, "GAME OVER\n", 10, 0);
                goto done;
            }

            if (shm->players_connected == MAX_PLAYERS &&
                shm->current_turn == id &&
                !shm->turn_completed)
                my_turn = 1;

            pthread_mutex_unlock(&shm->mutex);

            if (!my_turn)
                usleep(200000);
        }

        snprintf(msg, sizeof(msg),
                 "TURN: score=%d\nINPUT_REQUIRED\n",
                 shm->scores[id]);
        send(sock, msg, strlen(msg), 0);

        memset(buf, 0, sizeof(buf));
        if (recv(sock, buf, sizeof(buf), 0) <= 0)
            break;

        pthread_mutex_lock(&shm->mutex);

        if (shm->game_over) {
            pthread_mutex_unlock(&shm->mutex);
            break;
        }

        int roll = (rand() % 6) + 1;
        shm->scores[id] += roll;

        char logmsg[128];
        snprintf(logmsg, sizeof(logmsg),
                 "player %d rolled %d (total %d)",
                 id, roll, shm->scores[id]);
        send_log(logmsg);

        snprintf(msg, sizeof(msg),
                 "RESULT: rolled %d\n", roll);
        send(sock, msg, strlen(msg), 0);

        if (shm->scores[id] >= WIN_SCORE) {
            shm->game_over = 1;
            send_log("winner declared");
            save_scores();
        }

        shm->turn_completed = 1;
        pthread_mutex_unlock(&shm->mutex);
    }

done:
    close(sock);
    close(log_pipe[1]);
    _exit(0);
}

/* main */
int main(void)
{
    srand(time(NULL));

    struct sigaction sa;
    sa.sa_handler = reap_children;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    if (pipe(log_pipe) == -1)
        return 1;

    shm = mmap(NULL, sizeof(GameState),
               PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS,
               -1, 0);
    if (shm == MAP_FAILED)
        return 1;

    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->mutex, &a);

    shm->players_connected = 0;
    shm->current_turn = 0;
    shm->game_over = 0;
    shm->turn_completed = 0;

    load_scores();

    pthread_t log_tid, sched_tid;
    pthread_create(&log_tid, NULL, logger_thread, NULL);
    pthread_create(&sched_tid, NULL, scheduler_thread, NULL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
        return 1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return 1;

    if (listen(server_fd, 5) < 0)
        return 1;

    send_log("server ready");

    for (;;) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            continue;
        }

        pthread_mutex_lock(&shm->mutex);
        if (shm->players_connected < MAX_PLAYERS) {
            int id = shm->players_connected++;
            shm->scores[id] = 0;
            pthread_mutex_unlock(&shm->mutex);

            char m[64];
            snprintf(m, sizeof(m), "player %d connected", id);
            send_log(m);

            pid_t p = fork();
            if (p == 0) {
                close(server_fd);
                handle_client(id, client);
            }

            close(client);
        } else {
            pthread_mutex_unlock(&shm->mutex);
            close(client);
        }
    }
}
