// flower_client.c
// one flower node that will connect to the garden server
// basically this is one machine that listens for commands and moves its petals

#include "csapp.h"
#include "flower.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int running = 1;   // overall "should this client keep going" value
static int terminating = 0;   // sets when terminate gets received
static int connfd = -1;   // socket to the garden server

// my one global flower state for this client
static Flower g_flower;
// mutex so the motion thread and receiver thread dont mess up my business
static pthread_mutex_t flower_mutex = PTHREAD_MUTEX_INITIALIZER;

// basic helper so i dont have stray newlines
static void trim_newline(char *s) {
    if (s == NULL) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

// small wrapper around write that knows about this clients socket and name
static void sendLine(const char *line) {
    if (connfd < 0) return;
    ssize_t n = write(connfd, line, strlen(line));
    if (n < 0) {
        // this is per flower so just log with name if we have one
        printf("[%-8s] Warning: write() failed\n",
               (g_flower.name[0] ? g_flower.name : "client"));
    }
}

// quick snapshot print of all petal angles with a label
static void print_flower_snapshot(const char *label) {
    pthread_mutex_lock(&flower_mutex);

    int num = g_flower.num_petals;
    if (num > FLOWER_MAX_PETALS) num = FLOWER_MAX_PETALS;

    char name_copy[32];
    strncpy(name_copy, g_flower.name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';

    int angles[FLOWER_MAX_PETALS];
    for (int i = 0; i < num; i++) {
        angles[i] = (int)(g_flower.petals[i].current_angle + 0.5f);
    }

    pthread_mutex_unlock(&flower_mutex);

    printf("[%-8s] %s:", name_copy[0] ? name_copy : "flower", label);
    for (int i = 0; i < num; i++) {
        printf(" [%3d]", angles[i]);
    }
    printf("\n");
}

// handle a single command line from the server
static void handle_command_line(const char *line) {
    // skip leading spaces just inncase
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return; // empty
    }

    const char *name_tag = g_flower.name[0] ? g_flower.name : "flower";

    if (strcmp(line, "TERMINATE") == 0) {
        // server is telling this flower to gracefully shut down
        printf("[%-8s] cmd: TERMINATE (closing before shutdown)\n", name_tag);
        pthread_mutex_lock(&flower_mutex);
        Flower_applyCommand(&g_flower, "CLOSE");
        terminating = 1;
        pthread_mutex_unlock(&flower_mutex);
        // receiver thread will stop after this; motion thread will finish close
        return;
    }

    // normal commands just go straight into the flower logic
    pthread_mutex_lock(&flower_mutex);
    Flower_applyCommand(&g_flower, line);
    pthread_mutex_unlock(&flower_mutex);

    printf("[%-8s] cmd: %s\n", name_tag, line);
}

// thread that receives commands from the server and feeds them into handle_command_line function
static void* receiver_thread(void *arg) {
    (void)arg;
    char buf[MAXLINE];

    while (1) {
        ssize_t n = read(connfd, buf, MAXLINE - 1);
        if (n <= 0) {
            const char *name_tag = g_flower.name[0] ? g_flower.name : "flower";
            printf("[%-8s] server closed connection or read error.\n", name_tag);
            if (!terminating) {
                running = 0;
            }
            break;
        }

        buf[n] = '\0';

        // server can send more than one command in one read so split on newlines
        char *cursor = buf;
        while (running && cursor && *cursor) {
            // find end of line
            char *eol = strpbrk(cursor, "\r\n");
            if (eol != NULL) {
                *eol = '\0'; // terminate
            }

            // working copy for trimming
            char line_copy[128];
            strncpy(line_copy, cursor, sizeof(line_copy) - 1);
            line_copy[sizeof(line_copy) - 1] = '\0';
            trim_newline(line_copy);

            if (line_copy[0] != '\0') {
                handle_command_line(line_copy);
                if (terminating) {
                    // got terminate so motion thread will finish things before donezo
                    break;
                }
            }

            if (eol == NULL) {
                break; // no more lines in buff
            }

            // move cursor past the NL
            cursor = eol + 1;
            while (*cursor == '\r' || *cursor == '\n') {
                cursor++;
            }
        }

        if (terminating) {
            break;
        }
    }

    return NULL;
}

// this thread actually animates the petals over time and sends the status updates
// this part was cool
static void* motion_thread(void *arg) {
    (void)arg;

    const int dt_ms = 100;   // 100ms per update
    char status[256];
    int counter = 0;
    int was_moving = 0;
    int announced_closing = 0;

    while (running) {
        usleep(dt_ms * 1000);

        pthread_mutex_lock(&flower_mutex);

        // step physicsish side forward a bit
        Flower_update(&g_flower, dt_ms);
        // build a status line to send to the server
        Flower_buildStatus(&g_flower, status, sizeof(status));

        int num = g_flower.num_petals;
        if (num > FLOWER_MAX_PETALS) num = FLOWER_MAX_PETALS;

        // determine if any petal is still moving
        int moving = 0;
        for (int i = 0; i < num; i++) {
            float diff = g_flower.petals[i].target_angle
                       - g_flower.petals[i].current_angle;
            if (diff > 0.5f || diff < -0.5f) {
                moving = 1;
                break;
            }
        }

        char name_copy[32];
        strncpy(name_copy, g_flower.name, sizeof(name_copy) - 1);
        name_copy[sizeof(name_copy) - 1] = '\0';

        int angles[FLOWER_MAX_PETALS];
        for (int i = 0; i < num; i++) {
            angles[i] = (int)(g_flower.petals[i].current_angle + 0.5f);
        }

        int local_terminating = terminating;

        pthread_mutex_unlock(&flower_mutex);

        // always send satus to server
        sendLine(status);

        const char *name_tag = name_copy[0] ? name_copy : "flower";

        // after terminate print a one time "Im closing!!" thing and be done
        if (local_terminating && !announced_closing) {
            printf("\n[%-8s] closing before shutdown...\n", name_tag);
            announced_closing = 1;
        }

        // once terminate has been requested and everybod si closed, be done
        if (local_terminating && !moving) {
            printf("\n[%-8s] final (closed):", name_tag);
            for (int i = 0; i < num; i++) {
                printf(" [%3d]", angles[i]);
            }
            printf("\n");
            running = 0;
            break;
        }

        // print out movement in a way thats not insanely spammy, i made that mistake at first
        if (moving) {
            counter++;

            if (!was_moving) {
                // movement just started
                printf("\n[%-8s] moving:", name_tag);
                for (int i = 0; i < num; i++) {
                    printf(" [%3d]", angles[i]);
                }
                printf("\n");
            } else if (counter % 5 == 0) { // every half second print
                printf("[%-8s] moving:", name_tag);
                for (int i = 0; i < num; i++) {
                    printf(" [%3d]", angles[i]);
                }
                printf("\n");
            }
        } else {
            // not moving anymore
            if (was_moving && !local_terminating) {
                // just finished moving due to a normal command that isnt terminate
                printf("[%-8s] idle:", name_tag);
                for (int i = 0; i < num; i++) {
                    printf(" [%3d]", angles[i]);
                }
                printf("\n");
            }
        }

        was_moving = moving;
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s <server_host> <port> <flower_name> <num_petals>\n",
                argv[0]);
        exit(0);
    }

    char *server      = argv[1];
    char *port        = argv[2];
    char *flower_name = argv[3];
    int   num_petals  = atoi(argv[4]);

    if (num_petals <= 0 || num_petals > FLOWER_MAX_PETALS) {
        printf("Invalid num_petals (1..%d)\n", FLOWER_MAX_PETALS);
        exit(1);
    }

    connfd = Open_clientfd(server, port);
    if (connfd < 0) {
        printf("Could not connect to server.\n");
        exit(1);
    }

    printf("Connected to server %s:%s as flower '%s' with %d petals.\n",
           server, port, flower_name, num_petals);

    // set up the internal flower model with its name and # of petals
    Flower_init(&g_flower, flower_name, num_petals);

    // print initial state so its clear where were starting from
    print_flower_snapshot("initial");

    // send HELLO so the server can register this flower in its garden table
    char hello[128];
    snprintf(hello, sizeof(hello),
             "HELLO name=%s num_petals=%d\n", flower_name, num_petals);
    sendLine(hello);

    // one thread for listening to server commands one for motion and satus
    pthread_t recv_tid, motion_tid;
    pthread_create(&recv_tid, NULL, receiver_thread, NULL);
    pthread_create(&motion_tid, NULL, motion_thread, NULL);

    pthread_join(recv_tid, NULL);
    pthread_join(motion_tid, NULL);

    Close(connfd);
    printf("Flower '%s' shutting down.\n", flower_name);
    return 0;
}