// flower.h
// i'm not gonna comment this its just a header file, all the good stuff is in the flower.c

#ifndef FLOWER_H
#define FLOWER_H

#include <stddef.h>

#define FLOWER_MAX_PETALS 8

typedef struct {
    float current_angle;
    float target_angle;
    int   delay_ms;      // sequence start delay for this petal
} Petal;

typedef struct {
    char  name[32];
    int   num_petals;

    Petal petals[FLOWER_MAX_PETALS];

    float bloom_angle;       // "open" angle
    float close_angle;       // "closed" angle
    float speed_deg_per_sec; // how fast petals move

    int   seq_active;        // 0 = none, 1 = SEQ1, 2 = SEQ2
    int   elapsed_ms;        // used for delay_ms in sequences
} Flower;

// initialize a flower with a name and number of petals
void Flower_init(Flower *f, const char *name, int num_petals);

// apply a command string: "OPEN", "CLOSE", "SEQ1", or "SEQ2"
void Flower_applyCommand(Flower *f, const char *line);

// move petals toward their targets by dt_ms milliseconds
void Flower_update(Flower *f, int dt_ms);

// build a STATUS line into out:
//   STATUS name=<name> state=MOVING|IDLE petal_angles=...
// newline is added at the end if there is space
void Flower_buildStatus(const Flower *f, char *out, size_t out_size);

#endif