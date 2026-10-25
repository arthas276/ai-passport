// cheer_audio.c - chime worker: all bsp_audio calls serialize here
// (BSP rule: PCM IO blocks; button callbacks and the LVGL task never wait).
#include "cheer_audio.h"
#include "cheer_tone.h"
#include "bsp_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

#define CHUNK 512
#define CHEER_AUDIO_VOL 80

#define JOB_CHIME 1
#define JOB_FANFARE 2

static QueueHandle_t s_job_q;
static TaskHandle_t s_task;

static const uint8_t CHIME_NOTES[] = { 72, 76, 79 };           // C5 E5 G5
static const uint8_t FANFARE_NOTES[] = { 72, 76, 79, 84, 88 }; // + C6 E6

static void play_note(uint8_t midi, int ms, float amp) {
    cheer_tone_voice_t v;
    uint32_t total = (uint32_t)CHEER_TONE_SR * ms / 1000u;
    cheer_tone_start(&v, cheer_tone_freq(midi), amp, total);
    static int16_t buf[CHUNK];
    uint32_t left = total;
    while (left > 0) {
        uint32_t n = left < CHUNK ? left : CHUNK;
        cheer_tone_render(&v, buf, (int)n);
        if (bsp_audio_write(buf, (size_t)n * 2) != ESP_OK) return;
        left -= n;
    }
}

static void play_gap(int ms) {
    static int16_t buf[CHUNK];
    memset(buf, 0, sizeof(buf));
    uint32_t left = (uint32_t)CHEER_TONE_SR * ms / 1000u;
    while (left > 0) {
        uint32_t n = left < CHUNK ? left : CHUNK;
        bsp_audio_write(buf, (size_t)n * 2);
        left -= n;
    }
}

static void play_seq(const uint8_t *notes, int count, int note_ms,
                     int gap_ms, float amp) {
    for (int i = 0; i < count; i++) {
        play_note(notes[i], note_ms, amp);
        if (i + 1 < count) play_gap(gap_ms);
    }
}

static void audio_task(void *arg) {
    (void)arg;
    bsp_audio_set_format(CHEER_TONE_SR, 16, 1);
    bsp_audio_set_volume(CHEER_AUDIO_VOL);
    int job;
    for (;;) {
        if (xQueueReceive(s_job_q, &job, portMAX_DELAY) == pdTRUE) {
            if (job == JOB_FANFARE) {
                play_seq(FANFARE_NOTES, 5, 170, 25, 0.85f);
            } else {
                play_seq(CHIME_NOTES, 3, 150, 30, 0.8f);
            }
        }
    }
}

esp_err_t cheer_audio_init(void) {
    s_job_q = xQueueCreate(4, sizeof(int));
    if (!s_job_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "cheer_audio", 3072, NULL, 4, &s_task) != pdPASS) {
        vQueueDelete(s_job_q);
        s_job_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void cheer_audio_play_chime(bool fanfare) {
    if (!s_job_q) return;
    int job = fanfare ? JOB_FANFARE : JOB_CHIME;
    (void)xQueueSend(s_job_q, &job, 0);
}
