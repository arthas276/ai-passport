// tuner_settings.c - defaults, sanitisation and transpose cycling. PURE C.
#include "tuner_settings.h"

void tuner_settings_default(tuner_settings_t *s) {
    s->mode = TUNER_MODE_CHROMATIC;
    s->vol = TUNER_VOL_DEF;
    s->a4 = TUNER_A4_DEF;
    s->tp = TUNER_TP_OFF;
}

void tuner_settings_sanitize(tuner_settings_t *s) {
    if (s->mode >= TUNER_MODE_COUNT) s->mode = TUNER_MODE_CHROMATIC;
    if (s->vol > 100) s->vol = TUNER_VOL_DEF;
    if (s->a4 < TUNER_A4_MIN || s->a4 > TUNER_A4_MAX) s->a4 = TUNER_A4_DEF;
    switch (s->tp) {
    case TUNER_TP_OFF:
    case TUNER_TP_BB:
    case TUNER_TP_EB:
    case TUNER_TP_F:
        break;
    default:
        s->tp = TUNER_TP_OFF;
    }
}

uint8_t tuner_tp_next(uint8_t tp) {
    switch (tp) {
    case TUNER_TP_OFF: return TUNER_TP_BB;
    case TUNER_TP_BB:  return TUNER_TP_EB;
    case TUNER_TP_EB:  return TUNER_TP_F;
    default:           return TUNER_TP_OFF;
    }
}

const char *tuner_tp_short(uint8_t tp) {
    switch (tp) {
    case TUNER_TP_BB: return "Bb";
    case TUNER_TP_EB: return "Eb";
    case TUNER_TP_F:  return "F";
    default:          return "OFF";
    }
}
