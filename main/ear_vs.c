// ear_vs.c - VS BATTLE scoring (v1.7). PURE C.
#include "ear_vs.h"

void ear_vs_begin(ear_vs_t *vs, ear_mode_t mode, uint8_t level) {
    vs->mode = mode;
    vs->level = level;
    vs->index = 0;
    vs->p1_score = 0;
    vs->p2_score = 0;
}

uint8_t ear_vs_player(const ear_vs_t *vs) {
    return (uint8_t)(vs->index & 1u);   // P1 takes even questions, P2 odd
}

void ear_vs_record(ear_vs_t *vs, bool correct) {
    if (ear_vs_done(vs)) return;              // match over: ignore
    if (correct) {
        if (ear_vs_player(vs) == 0u) vs->p1_score++;
        else vs->p2_score++;
    }
    vs->index++;
}

bool ear_vs_done(const ear_vs_t *vs) {
    return vs->index >= EAR_VS_QUESTIONS;
}

int8_t ear_vs_winner(const ear_vs_t *vs) {
    if (vs->p1_score > vs->p2_score) return 0;
    if (vs->p2_score > vs->p1_score) return 1;
    return -1;
}
