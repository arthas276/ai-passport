// tuner_audio.c - single audio worker: continuous 16 kHz capture, pitch
// detection per frame, and a serial lock chime. Mirrors the ear-buddy
// worker ownership pattern (one task owns all bsp_audio calls).
#include "tuner_audio.h"
#include "tuner_pitch.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tuner_audio";

#define CHUNK 512
#define CUE_NOTE_MS 120

typedef struct { int cmd; } tuner_job_t;   // CMD_CUE only; capture is default
#define CMD_CUE 1

static QueueHandle_t s_job_q;
static QueueHandle_t s_event_q;
static TaskHandle_t s_task;

// Render the lock chime: two quick rising notes (E5 -> A5).
static void play_cue(void) {
    static const int16_t notes[] = { 76, 81 };   // MIDI E5, A5
    static int16_t buf[CHUNK];
    for (int k = 0; k < 2; k++) {
        float hz = tuner_note_freq(notes[k]);
        int32_t step = (int32_t)(hz * 1024.0f / TUNER_PITCH_SR);
        int32_t phase = 0;
        int left = TUNER_PITCH_SR * CUE_NOTE_MS / 1000u;
        while (left > 0) {
            int n = left < CHUNK ? left : CHUNK;
            for (int i = 0; i < n; i++) {
                // Integer triangle wave: audible on the tiny speaker,
                // no float/FPU cost on the ESP32-C3.
                int32_t ph = (phase >> 10) & 1023;
                int32_t tri = ph < 512 ? ph * 2 - 512 : 1536 - ph * 2;
                buf[i] = (int16_t)(tri * 24);
                phase += step;
            }
            if (bsp_audio_write(buf, n * 2) != ESP_OK) return;
            left -= n;
        }
    }
    tuner_event_t ev = { .kind = TUNER_EV_CUE_DONE };
    xQueueSend(s_event_q, &ev, 0);
}

// Capture/detect one frame and post the event. Returns false on read error.
static bool capture_frame(void) {
    static int16_t frame[TUNER_PITCH_FRAME];
    if (bsp_audio_read(frame, TUNER_PITCH_FRAME * 2) != ESP_OK) return false;
    tuner_pitch_result_t pr = tuner_pitch_detect(frame, TUNER_PITCH_FRAME);
    static float f1 = 0.0f, f2 = 0.0f;
    static int hist = 0;
    float f0 = pr.hz;
    if (hist >= 2) f0 = tuner_pitch_median3(f1, f2, pr.hz);
    f1 = f2;
    f2 = pr.hz;
    if (hist < 2) hist++;
    int16_t midi = -1;
    float cents = f0 > 0.0f ? tuner_note_cents(f0, &midi) : 0.0f;
    tuner_event_t ev = {
        .kind = TUNER_EV_PITCH, .hz = f0, .conf = pr.conf,
        .midi = midi, .cents = cents,
    };
    xQueueSend(s_event_q, &ev, 0);
    return true;
}

static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        tuner_job_t job;
        if (xQueueReceive(s_job_q, &job, 0) == pdTRUE && job.cmd == CMD_CUE) {
            play_cue();
            continue;   // drain any queued cue before resuming capture
        }
        if (!capture_frame()) {
            ESP_LOGW(TAG, "capture read failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t tuner_audio_init(QueueHandle_t event_q) {
    if (s_task) return ESP_OK;   // idempotent
    s_event_q = event_q;
    esp_err_t bsp = bsp_audio_init();
    if (bsp != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(bsp));
        // keep going: worker still runs, capture will keep failing loudly
    }
    if (bsp_audio_set_format(TUNER_PITCH_SR, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "set_format 16k failed");
    }
    bsp_audio_set_volume(80);
    s_job_q = xQueueCreate(2, sizeof(tuner_job_t));
    if (!s_job_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "tuner_audio", 6144, NULL, 4, &s_task)
        != pdPASS) {
        vQueueDelete(s_job_q);
        s_job_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void tuner_audio_cue(void) {
    tuner_job_t job = { .cmd = CMD_CUE };
    xQueueSend(s_job_q, &job, 0);   // drop when busy: capture is the priority
}
