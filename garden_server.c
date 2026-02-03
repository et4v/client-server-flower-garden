// garden_server.c
// behold my little garden server that controls up to 64 of flower clients at once

#include "csapp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>   // for toupper

#define MAX_FLOWERS 64

// each flower that connects gets one of these slots in the garden array
typedef struct {
    int  in_use;
    int  connfd;
    char name[32];
    char last_status[256];  // most recent status line from that flower which updates often
} FlowerEntry;

// this is my garden :) its fixed size list of possible flowers
static FlowerEntry garden[MAX_FLOWERS];
// mutex so multiple threads enter my garden at the same time
static pthread_mutex_t garden_mutex = PTHREAD_MUTEX_INITIALIZER;

// basic helper to strip off newline
static void trim_newline(char *s) {
    if (s == NULL) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

// tiny wrapper around write so I dont need to repeat the error check every time
static void sendLine(int fd, const char *line) {
    ssize_t n = write(fd, line, strlen(line));
    if (n < 0) {
        printf("Warning: write() failed to fd %d\n", fd);
    }
}

// just displays all the commands in case needed
// i made this also display if an invalid command is enteed
static void print_help(void) {
    printf("Commands:\n");
    printf("  OPEN all|<name>        Open all flowers or one flower\n");
    printf("  CLOSE all|<name>       Close all flowers or one flower\n");
    printf("  SEQ1 all|<name>        Petal sequence 1 (left-to-right)\n");
    printf("  SEQ2 all|<name>        Petal sequence 2 (outside-in)\n");
    printf("  TERMINATE all|<name>   Close and terminate clients\n");
    printf("  BLOOM                  Random sequence per flower, staggered\n");
    printf("  LIST                   List connected flowers\n");
    printf("  STATUS                 Show most recent STATUS per flower\n");
    printf("  HELP                   Show this help text\n");
    printf("  QUIT                   CLOSE all, TERMINATE all, and exit\n");
}

// when a client sends HELLO name=blahblahblah that data gets stored
static void register_flower(int connfd, const char *name) {
    pthread_mutex_lock(&garden_mutex);

    // if we already have this name just refresh its fd / status
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use && strcmp(garden[i].name, name) == 0) {
            garden[i].connfd = connfd;
            garden[i].last_status[0] = '\0';
            pthread_mutex_unlock(&garden_mutex);
            printf("Updated flower '%s' (fd=%d)\n", name, connfd);
            return;
        }
    }

    // otherwise find an empty slot and claim it
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (!garden[i].in_use) {
            garden[i].in_use = 1;
            garden[i].connfd = connfd;
            strncpy(garden[i].name, name, sizeof(garden[i].name) - 1);
            garden[i].name[sizeof(garden[i].name) - 1] = '\0';
            garden[i].last_status[0] = '\0';
            pthread_mutex_unlock(&garden_mutex);
            printf("Registered flower '%s' (fd=%d)\n", name, connfd);
            return;
        }
    }

    // if we are here the garden is full
    pthread_mutex_unlock(&garden_mutex);
    printf("No space left in garden for flower '%s'\n", name);
}

// when a client disconnects this clears out that spot in the garden
static void unregister_flower(int connfd) {
    pthread_mutex_lock(&garden_mutex);
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use && garden[i].connfd == connfd) {
            printf("Removing flower '%s' (fd=%d)\n", garden[i].name, connfd);
            garden[i].in_use = 0;
            break;
        }
    }
    pthread_mutex_unlock(&garden_mutex);
}

// send the same command line to every flower connected
static void broadcast_command(const char *cmd) {
    pthread_mutex_lock(&garden_mutex);
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use) {
            sendLine(garden[i].connfd, cmd);
        }
    }
    pthread_mutex_unlock(&garden_mutex);
}

// send a command line to just one flower by name
static void send_to_one(const char *name, const char *cmd) {
    int found = 0;
    pthread_mutex_lock(&garden_mutex);
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use && strcmp(garden[i].name, name) == 0) {
            sendLine(garden[i].connfd, cmd);
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&garden_mutex);
    if (!found) {
        printf("No flower named '%s' is connected.\n", name);
    }
}

// lists all flowers,,, reallt just for testing and debugging
static void list_flowers() {
    pthread_mutex_lock(&garden_mutex);
    printf("Current flowers in the garden:\n");
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use) {
            printf("  %s (fd=%d)\n", garden[i].name, garden[i].connfd);
        }
    }
    pthread_mutex_unlock(&garden_mutex);
}

// shows whatever the last status was for each flower
static void print_status_all() {
    pthread_mutex_lock(&garden_mutex);
    printf("Flower Status:\n");
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use) {
            if (garden[i].last_status[0] != '\0') {
                printf("  %s: %s\n", garden[i].name, garden[i].last_status);
            } else {
                printf("  %s: (no status yet)\n", garden[i].name);
            }
        }
    }
    pthread_mutex_unlock(&garden_mutex);
}

// continure checking garden until everybody is gone
// used during quit so the server doesnt end before clients finish closing
static void wait_for_all_flowers_to_terminate(void) {
    while (1) {
        pthread_mutex_lock(&garden_mutex);
        int active = 0;
        for (int i = 0; i < MAX_FLOWERS; i++) {
            if (garden[i].in_use) {
                active = 1;
                break;
            }
        }
        pthread_mutex_unlock(&garden_mutex);

        if (!active) {
            printf("All flowers have closed and disconnected.\n");
            break;
        }

        printf("Waiting for flowers to close and disconnect...\n");
        sleep(1);
    }
}

// this is the "BLOOM" garden command where each flower gets SEQ1 or SEQ2 chosen randomly
// with an also random delay in between so they don't all move at exactly the same time
static void run_garden_bloom_sequence(void) {
    int fds[MAX_FLOWERS];
    int count = 0;

    pthread_mutex_lock(&garden_mutex);
    for (int i = 0; i < MAX_FLOWERS; i++) {
        if (garden[i].in_use) {
            fds[count++] = garden[i].connfd;
        }
    }
    pthread_mutex_unlock(&garden_mutex);

    if (count == 0) {
        printf("No flowers connected for BLOOM.\n");
        return;
    }

    printf("Starting BLOOM sequence for %d flowers.\n", count);

    for (int i = 0; i < count; i++) {
        const char *cmd = (rand() % 2 == 0) ? "SEQ1\n" : "SEQ2\n";
        sendLine(fds[i], cmd);

        int delay_ms = 400 + (rand() % 500);  // just anywhere between 400 and 899 ms
        usleep(delay_ms * 1000);
    }

    printf("BLOOM commands sent.\n");
}

// one thread per client lives here and this handles incoming flower data
static void* client_thread(void *arg) {
    int connfd = *(int *)arg;
    free(arg);

    char buf[MAXLINE];

    // first line should be HELLO with the flower name.. this doesnt get shown anywhere its just for
    // registration purposes
    ssize_t n = read(connfd, buf, MAXLINE - 1);
    if (n <= 0) {
        Close(connfd);
        return NULL;
    }
    buf[n] = '\0';
    trim_newline(buf);

    if (strncmp(buf, "HELLO", 5) == 0) {
        char *name_ptr = strstr(buf, "name=");
        if (name_ptr != NULL) {
            name_ptr += 5;
            char flower_name[32];
            int i = 0;
            while (*name_ptr != '\0' && *name_ptr != ' ' &&
                   i < (int)sizeof(flower_name) - 1) {
                flower_name[i++] = *name_ptr++;
            }
            flower_name[i] = '\0';
            register_flower(connfd, flower_name);
        } else {
            printf("HELLO missing name, fd=%d\n", connfd);
        }
    } else {
        printf("Expected HELLO, got: %s\n", buf);
    }

    // I mostly care about status lines so I can show a snapshot if needed
    while (1) {
        n = read(connfd, buf, MAXLINE - 1);
        if (n <= 0) break;

        buf[n] = '\0';
        trim_newline(buf);

        if (strncmp(buf, "STATUS", 6) == 0) {
            pthread_mutex_lock(&garden_mutex);
            for (int i = 0; i < MAX_FLOWERS; i++) {
                if (garden[i].in_use && garden[i].connfd == connfd) {
                    strncpy(garden[i].last_status, buf,
                            sizeof(garden[i].last_status) - 1);
                    garden[i].last_status[sizeof(garden[i].last_status) - 1] = '\0';
                    break;
                }
            }
            pthread_mutex_unlock(&garden_mutex);
        } else {
            // anything else the client says gets logged
            printf("From client %d: %s\n", connfd, buf);
        }
    }

    unregister_flower(connfd);
    Close(connfd);
    return NULL;
}

// this thread is just watching stdin and processing the commands entered into server terminal
static void* command_thread(void *arg) {
    (void)arg;

    char line[128];

    printf("~Blooming Garden Controller~\n");
    printf("Type HELP for commands.\n\n"); // i thought this was a nice touch

    while (1) {
        printf("garden> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) break;
        trim_newline(line);

        if (line[0] == '\0') continue;

        // parse into action and target..if there is one because its not reqired
        char parsebuf[128];
        strncpy(parsebuf, line, sizeof(parsebuf) - 1);
        parsebuf[sizeof(parsebuf) - 1] = '\0';

        char *first  = strtok(parsebuf, " \t");
        char *second = strtok(NULL, " \t");

        if (!first) continue;

        char action[32];
        char target[32];

        strncpy(action, first, sizeof(action) - 1);
        action[sizeof(action) - 1] = '\0';

        if (second) {
            strncpy(target, second, sizeof(target) - 1);
            target[sizeof(target) - 1] = '\0';
        } else {
            target[0] = '\0';
        }

        // make the action uppercase so commands are not case sensitive
        for (int i = 0; action[i] != '\0'; i++) {
            action[i] = (char)toupper((unsigned char)action[i]);
        }

        // no target means its one of the simple commands
        if (target[0] == '\0') {
            if (strcmp(action, "LIST") == 0) {
                list_flowers();
                continue;
            }
            if (strcmp(action, "STATUS") == 0) {
                print_status_all();
                continue;
            }
            if (strcmp(action, "HELP") == 0) {
                print_help();
                continue;
            }
            if (strcmp(action, "BLOOM") == 0) {
                run_garden_bloom_sequence();
                continue;
            }
            if (strcmp(action, "QUIT") == 0) {
                printf("Closing all flowers before shutdown...\n");
                broadcast_command("CLOSE\n");      // tell everyone to close

                printf("Sending TERMINATE to all flowers and waiting for them to close.\n");
                broadcast_command("TERMINATE\n");  // graceful shutdown on clients

                wait_for_all_flowers_to_terminate();
                printf("Shutting down server.\n");
                exit(0);
            }

            printf("Unknown command: %s\n", line);
            print_help();
            continue;
        }

        // here we use a target which can be ALL or a specific flower name
        int to_all = 0;
        {
            char tgt_upper[32];
            strncpy(tgt_upper, target, sizeof(tgt_upper) - 1);
            tgt_upper[sizeof(tgt_upper) - 1] = '\0';
            for (int i = 0; tgt_upper[i] != '\0'; i++) {
                tgt_upper[i] = (char)toupper((unsigned char)tgt_upper[i]);
            }
            if (strcmp(tgt_upper, "ALL") == 0) {
                to_all = 1;
            }
        }

        // these are the actions I actually forward to the client sockets
        if (strcmp(action, "OPEN") == 0 ||
            strcmp(action, "CLOSE") == 0 ||
            strcmp(action, "SEQ1") == 0 ||
            strcmp(action, "SEQ2") == 0 ||
            strcmp(action, "TERMINATE") == 0) {

            char sendbuf[32];
            snprintf(sendbuf, sizeof(sendbuf), "%.30s\n", action);

            if (to_all)
                broadcast_command(sendbuf);
            else
                send_to_one(target, sendbuf);

        } else {
            printf("Unknown action: %s\n", action);
            print_help();
        }
    }

    return NULL;
}

// main just sets up the listening socket, spins off the command thread,
// and then sits in an accept() loop making a client thread for each flower
int main(int argc, char **argv) {
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(0);
    }

    srand((unsigned int)time(NULL));  // seed RNG for BLOOM

    listenfd = Open_listenfd(argv[1]);

    pthread_t cmd_tid;
    pthread_create(&cmd_tid, NULL, command_thread, NULL);
    pthread_detach(cmd_tid);

    printf("Garden server listening on port %s\n\n", argv[1]);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        int *connfdp = malloc(sizeof(int));
        if (connfdp == NULL) {
            printf("malloc failed\n");
            continue;
        }

        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        Getnameinfo((SA *)&clientaddr, clientlen,
                    client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        printf("New connection from (%s, %s), fd=%d\n",
               client_hostname, client_port, *connfdp);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, connfdp) != 0) {
            printf("pthread_create failed\n");
            Close(*connfdp);
            free(connfdp);
            continue;
        }
        pthread_detach(tid);
    }

    return 0;
}