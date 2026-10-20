// ear_vs.h - VS BATTLE scoring for EAR BUDDY (v1.7). Two players alternate
// answering quiz questions on one device; the shared pet earns XP from every
// correct answer either player gives. Pure C header: host-testable.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ear_game.h"

// Match shape: 10 questions total, alternating, P1 opens.
#define EAR_VS_QUESTIONS EAR_QUESTIONS_PER_SESSION

typedef struct {
    ear_mode_t mode;    // question source for the match
    uint8_t level;      // 1..3, fixed for the whole match
    uint8_t index;      // 0-based question index 0..EAR_VS_QUESTIONS-1
    uint8_t p1_score;   // correct answers by player 1
    uint8_t p2_score;   // correct answers by player 2
} ear_vs_t;

// Start a match. mode must be a quiz mode (INTERVAL/CHORD); level 1..3.
void ear_vs_begin(ear_vs_t *vs, ear_mode_t mode, uint8_t level);
// Player answering question vs->index: 0 = P1, 1 = P2.
uint8_t ear_vs_player(const ear_vs_t *vs);
// Record the current player's answer and advance the question index.
void ear_vs_record(ear_vs_t *vs, bool correct);
// True when all questions were played.
bool ear_vs_done(const ear_vs_t *vs);
// Match verdict: 0 = P1 wins, 1 = P2 wins, -1 = draw. Meaningful only when
// ear_vs_done() is true.
int8_t ear_vs_winner(const ear_vs_t *vs);
