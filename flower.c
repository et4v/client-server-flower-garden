// flower.c
// implementation of flower petal behavior mwahahaha

// so I decided to keep all the actual flower petal logic in its own file
// just because the networking part was already getting big and kind of chaotic
// this keeps all the angle math and the sequences in one place where it is easier to think about it

// also it means if I ever wanted to port this to a microcontroller or something
// I could reuse all of this motion code without dragging the socket stuff with it
// basically the flower struct is its own thing and the client and server just tell it what to do

#include "flower.h"
#include <string.h>
#include <stdio.h>

// helper to strip newline characters off the end of a c string
// this keeps later string comparisons and parsing from being weird
static void trim_newline(char *s) {
    if (s == NULL) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

// tiny absolute value helper for floats
// I just did this so I do not have to pull in the full math library
static float myabsf(float x) {
    return (x < 0.0f) ? -x : x;
}

// set up the flower struct with a name and a number of petals
// clamps the petal count into a safe range
// sets all petals to the closed angle and defines the basic bloom and close targets
// also picks a default speed so the movement looks smooth instead of instant
void Flower_init(Flower *f, const char *name, int num_petals) {
    if (f == NULL) return;

    if (num_petals < 1) num_petals = 1;
    if (num_petals > FLOWER_MAX_PETALS) num_petals = FLOWER_MAX_PETALS;
    f->num_petals = num_petals;

    if (name != NULL) {
        strncpy(f->name, name, sizeof(f->name) - 1);
        f->name[sizeof(f->name) - 1] = '\0';
    } else {
        f->name[0] = '\0';
    }

    f->bloom_angle       = 80.0f;
    f->close_angle       = 5.0f;
    f->speed_deg_per_sec = 15.0f;   // slower smoother bloom

    f->seq_active = 0;
    f->elapsed_ms = 0;

    for (int i = 0; i < FLOWER_MAX_PETALS; i++) {
        f->petals[i].current_angle = f->close_angle;
        f->petals[i].target_angle  = f->close_angle;
        f->petals[i].delay_ms      = 0;
    }
}

// sequence one is a left to right stagger
// each petal has a slightly later delay so the flower ripples open from one side
// important detail is that I do not reset current_angle here
// the petals always move from wherever they are now to the bloom angle
static void Flower_startSeq1(Flower *f) {
    f->seq_active = 1;
    f->elapsed_ms = 0;

    int gap = 200; // ms between petals

    for (int i = 0; i < f->num_petals; i++) {
        f->petals[i].delay_ms     = i * gap;
        // do not reset current_angle
        f->petals[i].target_angle = f->bloom_angle;
    }
}

// sequence two is an outside in pair pattern
// the two petals on the ends start first then the next pair inward and so on
// again I only set target_angle so the motion is always from the current pose
static void Flower_startSeq2(Flower *f) {
    f->seq_active = 2;
    f->elapsed_ms = 0;

    int gap = 200;
    int left = 0;
    int right = f->num_petals - 1;
    int step_index = 0;

    while (left <= right) {
        int delay_for_step = step_index * gap;

        f->petals[left].delay_ms     = delay_for_step;
        f->petals[left].target_angle = f->bloom_angle;

        if (right != left) {
            f->petals[right].delay_ms     = delay_for_step;
            f->petals[right].target_angle = f->bloom_angle;
        }

        left++;
        right--;
        step_index++;
    }
}

// this takes a text command line like OPEN CLOSE SEQ1 SEQ2
// trims it, cleans up whitespace and then updates the flower targets

// OPEN means everybody heads toward the bloom angle
// CLOSE means everybody heads toward the closed angle
// SEQ1 and SEQ2 kick off the two fancier staggered animations i customized
void Flower_applyCommand(Flower *f, const char *line) {
    if (f == NULL || line == NULL) return;

    char buffer[128];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    trim_newline(buffer);

    char *p = buffer;
    while (*p == ' ' || *p == '\t') p++;

    if (*p == '\0') return;

    if (strcmp(p, "OPEN") == 0) {
        f->seq_active = 0;
        f->elapsed_ms = 0;
        for (int i = 0; i < f->num_petals; i++) {
            f->petals[i].delay_ms     = 0;
            f->petals[i].target_angle = f->bloom_angle;
        }
    }
    else if (strcmp(p, "CLOSE") == 0) {
        f->seq_active = 0;
        f->elapsed_ms = 0;
        for (int i = 0; i < f->num_petals; i++) {
            f->petals[i].delay_ms     = 0;
            f->petals[i].target_angle = f->close_angle;
        }
    }
    else if (strcmp(p, "SEQ1") == 0) {
        Flower_startSeq1(f);
    }
    else if (strcmp(p, "SEQ2") == 0) {
        Flower_startSeq2(f);
    }
    else {
        // unknown commands are just ignored so the client does not explode!
    }
}

// below is the little physics tick for the flower mwahahahaha the cool part

// dt_ms is jsut how many milliseconds passed since the last update call
// it uses the speed in degrees per second and moves each petal toward its target
// if a sequence is active it also keeps track of elapsed time to honor the per petal delays
void Flower_update(Flower *f, int dt_ms) {
    if (f == NULL) return;
    if (dt_ms <= 0) return;

    if (f->seq_active != 0) {
        f->elapsed_ms += dt_ms;
    }

    float dt_sec = (float)dt_ms / 1000.0f;
    float step   = f->speed_deg_per_sec * dt_sec;
    if (step <= 0.0f) return;

    for (int i = 0; i < f->num_petals; i++) {
        Petal *p = &f->petals[i];

        // if we are in a sequence and this petal has not reached its delay time yet just skip it
        if (f->seq_active != 0 && f->elapsed_ms < p->delay_ms) {
            continue;
        }

        float cur  = p->current_angle;
        float tgt  = p->target_angle;
        float diff = tgt - cur;

        // move toward the target but never overshoot it
        if (diff > 0.0f) {
            if (diff <= step) cur = tgt;
            else cur += step;
        } else if (diff < 0.0f) {
            if (-diff <= step) cur = tgt;
            else cur -= step;
        }

        p->current_angle = cur;
    }
}

// this builds a status line that the client can send back to the server
// the format is text based so it is easy to log and debug
// includes flower name a simple state and the current petal angles
void Flower_buildStatus(const Flower *f, char *out, size_t out_size) {
    if (out == NULL || out_size == 0 || f == NULL) return;

    out[0] = '\0';

    // decide if the flower is idle or moving by checking how far each petal is from its target
    const char *state = "IDLE";
    for (int i = 0; i < f->num_petals; i++) {
        float diff = f->petals[i].target_angle - f->petals[i].current_angle;
        if (myabsf(diff) > 0.5f) {
            state = "MOVING";
            break;
        }
    }

    int written = snprintf(out, out_size,
        "STATUS name=%s state=%s petal_angles=",
        (f->name[0] != '\0') ? f->name : "noname",
        state);

    if (written < 0 || (size_t)written >= out_size) {
        if (out_size > 0) out[out_size - 1] = '\0';
        return;
    }

    size_t used = (size_t)written;

    // append each petal angle as an integer separated by commas
    for (int i = 0; i < f->num_petals; i++) {
        int ang = (int)(f->petals[i].current_angle + 0.5f);
        int n = snprintf(out + used, out_size - used,
                         (i == f->num_petals - 1) ? "%d" : "%d,",
                         ang);
        if (n < 0) break;
        used += (size_t)n;
        if (used >= out_size) {
            out[out_size - 1] = '\0';
            return;
        }
    }

    // try to add a newline at the end without overflowing the buffer
    if (used + 1 < out_size) {
        out[used] = '\n';
        if (used + 2 < out_size) {
            out[used + 1] = '\0';
        } else {
            out[out_size - 1] = '\0';
        }
    } else {
        out[out_size - 1] = '\0';
    }
}