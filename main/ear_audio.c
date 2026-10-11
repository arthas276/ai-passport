// ear_audio.c - single audio worker task: all bsp_audio calls serialize here
// (BSP rule: PCM IO blocks; format changes must go through the close/open
// sequence, so playback and capture at the same 16 kHz still never overlap).
#include "ear_audio.h"
#include "ear_audio.h"
#include "bsp_audio.h"
#include "bsp_display.h" // not used here; events go through the queue
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ear_audio";

#define CHUNK 512                    // render/read chunk in samples
#define NOTE_MS 650                  // interval note length (v1.3: longer)
#define GAP_MS 120                   // gap between interval notes
#define CHORD_MS 1250                // chord length (v1.3: longer)
#define TARGET_MS 950                // sing target preview length (v1.3)
#define SING_WINDOW_MS 4000          // attempt window
#define SING_HOLD_MS 1000            // in-tune time needed for PASS

// Playback request, one job = one sound event.
typedef struct {
    int cmd;                         // CMD_* below
    uint8_t root;
    uint8_t kind;
    float target_hz;
    float tol_cents;
} ear_job_t;

#define CMD_QUIT 0
#define CMD_QUESTION 1
#define CMD_TARGET 2
#define CMD_CUE 3
#define CMD_SING 4
#define CMD_CANCEL_SING 5
#define CUE_MAX_NOTES 8

// Worker state. s_task/s_stopped follow the demo_audio.c ownership pattern:
// the task stays alive after quit until it gives s_stopped; only the owner
// deletes/replaces the handle.
static QueueHandle_t s_job_q;
static QueueHandle_t s_event_q;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_stopped;
static volatile bool s_cancel;      // abort current playback/sing
static uint8_t s_volume = 80;

// Render one note (midi, ms) with one voice into chunked writes.
static void play_note(uint8_t midi, int ms, float amp) {
    ear_tone_voice_t v;
    ear_tone_start(&v, ear_note_freq(midi), amp, EAR_TONE_SR * ms / 1000u);
    int left = EAR_TONE_SR * ms / 1000u;
    static int16_t buf[CHUNK];
    int64_t t0 = esp_timer_get_time();   // DIAG: real-time budget check
    while (left > 0 && !s_cancel) {
        int n = left < CHUNK ? left : CHUNK;
        ear_tone_render(&v, buf, n);
        if (bsp_audio_write(buf, n * 2) != ESP_OK) {
            ESP_LOGE(TAG, "write failed");
            return;
        }
        left -= n;
    }
    // DIAG: a 450 ms note must complete in ~450 ms; way over = CPU starvation
    // (render can't keep pace) -> I2S DMA underruns -> crackle/distortion.
    int64_t dt = esp_timer_get_time() - t0;
    if (dt > (int64_t)ms * 1000 + 20000) {
        ESP_LOGW(TAG, "slow note: %d ms budget took %lld us", ms,
                 (long long)dt);
    }
}

// Silence for ms milliseconds.
static void play_gap(int ms) {
    static int16_t buf[CHUNK];
    memset(buf, 0, sizeof(buf));
    int left = EAR_TONE_SR * ms / 1000u;
    while (left > 0 && !s_cancel) {
        int n = left < CHUNK ? left : CHUNK;
        bsp_audio_write(buf, n * 2);
        left -= n;
    }
}

// Render the audio for one quiz question (interval or chord).
static void play_question(uint8_t root, uint8_t kind_idx) {
    const ear_kind_info_t *k = &EAR_KINDS[kind_idx];
    if (k->len == 0) {                        // interval: two notes in turn
        play_note(root, NOTE_MS, 0.9f);
        play_gap(GAP_MS);
        play_note(root + k->semis, NOTE_MS, 0.9f);
    } else {                                  // chord: notes together
        ear_tone_voice_t vo[EAR_TONE_MAX_VOICES];
        int16_t tmp[CHUNK];
        static int16_t out[CHUNK];
        int total = EAR_TONE_SR * CHORD_MS / 1000u;
        for (int i = 0; i < k->len; i++) {
            ear_tone_start(&vo[i], ear_note_freq(root + k->chord[i]), 0.55f, total);
        }
        int left = total;
        while (left > 0 && !s_cancel) {
            int n = left < CHUNK ? left : CHUNK;
            memset(out, 0, n * 2);          // static buf: re-zero per chunk
            for (int i = 0; i < k->len; i++) {
                ear_tone_render(&vo[i], tmp, n);
                ear_tone_mix(out, tmp, n);
            }
            bsp_audio_write(out, n * 2);
            left -= n;
        }
    }
}

// Render a cue jingle (list of MIDI notes, 0-terminated, 90 ms each).
static void play_cue(const uint8_t *notes) {
    for (int i = 0; notes[i] != 0; i++) {
        play_note(notes[i], EAR_JINGLE_NOTE_MS, 0.8f);
        if (notes[i + 1] != 0) play_gap(EAR_JINGLE_GAP_MS);
    }
}

// Sing attempt: capture 16 kHz frames, detect pitch, accumulate hold time.
// Posts EV_PITCH per frame; PASS when held in-tune >= SING_HOLD_MS within a
// SING_WINDOW_MS budget; FAIL on window exhaustion or cancel.
static void run_sing(float target_hz, float tol_cents) {
    if (bsp_audio_set_format(EAR_PITCH_SR, 16, 1) != ESP_OK) {
        ear_audio_event_t ev = { .kind = EAR_AUDIO_EV_SING_FAIL };
        xQueueSend(s_event_q, &ev, 0);
        return;
    }
    bsp_audio_set_volume(s_volume);
    static int16_t frame[EAR_PITCH_FRAME];
    const int frame_ms = (int)(EAR_PITCH_FRAME * 1000 / EAR_PITCH_SR);  // 64 ms
    int hold = 0, elapsed = 0;
    float f1 = 0.0f, f2 = 0.0f;   // last two raw detections; median-3 keeps
    int hist = 0;                 // one glitch frame from killing the hold
    while (elapsed < SING_WINDOW_MS && !s_cancel) {
        if (bsp_audio_read(frame, EAR_PITCH_FRAME * 2) != ESP_OK) break;
        ear_pitch_result_t pr = ear_pitch_detect(frame, EAR_PITCH_FRAME);
        elapsed += frame_ms;
        float f0 = pr.hz;
        if (hist >= 2) f0 = ear_pitch_median3(f1, f2, pr.hz);
        f1 = f2; f2 = pr.hz;
        if (hist < 2) hist++;
        // Octave-equivalent grading (v1.1): the right pitch class in any
        // octave counts; the UI gets the folded count for an OCT hint.
        int8_t oct = 0;
        float cents = ear_pitch_octave_cents(ear_pitch_cents(f0, target_hz),
                                             &oct);
        bool in_tune = (f0 > 0.0f) && (cents <= tol_cents) && (cents >= -tol_cents);
        if (in_tune) {
            hold += frame_ms;
            if (hold >= SING_HOLD_MS) {
                ear_audio_event_t ev = { .kind = EAR_AUDIO_EV_SING_PASS };
                xQueueSend(s_event_q, &ev, 0);
                return;
            }
        } else {
            hold = 0;
        }
        ear_audio_event_t ev = {
            .kind = EAR_AUDIO_EV_PITCH, .hz = f0, .conf = pr.conf,
            .cents = cents, .pct = (uint8_t)(hold * 100 / SING_HOLD_MS),
            .oct = oct,
        };
        xQueueSend(s_event_q, &ev, 0);
    }
    ear_audio_event_t ev = { .kind = EAR_AUDIO_EV_SING_FAIL, .pct = 0 };
    xQueueSend(s_event_q, &ev, 0);
}

// Post PLAY_DONE to the app (unless the event queue is full - UI polls the
// screen state, a dropped event would stall the flow, so this must send).
static void post_play_done(void) {
    ear_audio_event_t ev = { .kind = EAR_AUDIO_EV_PLAY_DONE };
    xQueueSend(s_event_q, &ev, pdMS_TO_TICKS(200));
}
// Worker main loop: drain jobs, one sound/sing at a time.
static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        ear_job_t job;
        if (xQueueReceive(s_job_q, &job, portMAX_DELAY) != pdTRUE) continue;
        s_cancel = false;
        switch (job.cmd) {
        case CMD_QUESTION:
            if (bsp_audio_set_format(EAR_TONE_SR, 16, 1) == ESP_OK) {
                bsp_audio_set_volume(s_volume);
                play_question(job.root, job.kind);
            }
            post_play_done();
            break;
        case CMD_TARGET:
            if (bsp_audio_set_format(EAR_TONE_SR, 16, 1) == ESP_OK) {
                bsp_audio_set_volume(s_volume);
                play_note(job.root, TARGET_MS, 0.9f);
            }
            post_play_done();
            break;
        case CMD_CUE: {
            // job.root carries the cue id (0 correct / 1 wrong / 2 evolve).
            static const uint8_t *CUES[3] = {
                EAR_CUE_CORRECT, EAR_CUE_WRONG, EAR_CUE_EVOLVE,
            };
            int id = job.root < 3 ? job.root : 0;
            if (bsp_audio_set_format(EAR_TONE_SR, 16, 1) == ESP_OK) {
                bsp_audio_set_volume(s_volume);
                play_cue(CUES[id]);
            }
            post_play_done();
            break;
        }
        case CMD_SING:
            // run_sing handles set_format(16 kHz) itself; calling it twice
            // would trigger the codec close/open sequence for nothing.
            run_sing(job.target_hz, job.tol_cents);
            break;
        case CMD_QUIT:
            xSemaphoreGive(s_stopped);
            vTaskSuspend(NULL);
            return;            // not reached; keeps gcc happy about noreturn
        }
    }
}

// Task body wrapper: owns the stopped-ack pattern from demo_audio.c.
static void audio_task_entry(void *arg) {
    audio_task(arg);
    for (;;) vTaskSuspend(NULL);
}

// ---- public API ----
esp_err_t ear_audio_init(QueueHandle_t event_q) {
    if (s_task) return ESP_OK;   // idempotent
    s_event_q = event_q;
    // BSP audio hardware (I2C->ES8311, full-duplex I2S, codec dev) must be
    // initialized once up front: bsp_audio_set_format() returns
    // ESP_ERR_INVALID_STATE without it, so the first job would silently
    // produce no sound and no capture (see bsp_audio.c s_initialized).
    esp_err_t bsp = bsp_audio_init();
    if (bsp != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(bsp));
        // keep going: worker still posts PLAY_DONE/SING_FAIL, UI flow lives
    }
    s_job_q = xQueueCreate(4, sizeof(ear_job_t));
    if (!s_job_q) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task_entry, "ear_audio", 6144, NULL, 4, &s_task)
        != pdPASS) {
        vQueueDelete(s_job_q);
        s_job_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ear_audio_play_question(uint8_t root_midi, ear_kind_t kind) {
    ear_job_t job = {
        .cmd = CMD_QUESTION, .root = root_midi, .kind = (uint8_t)kind,
    };
    xQueueSend(s_job_q, &job, 0);   // drop when busy: UI re-issues
}

void ear_audio_play_target(uint8_t midi) {
    ear_job_t job = { .cmd = CMD_TARGET, .root = midi };
    xQueueSend(s_job_q, &job, 0);
}

void ear_audio_play_cue(int cue_id) {
    ear_job_t job = { .cmd = CMD_CUE, .root = (uint8_t)cue_id };
    xQueueSend(s_job_q, &job, 0);
}

void ear_audio_sing_start(float target_hz, float tol_cents) {
    ear_job_t job = {
        .cmd = CMD_SING, .target_hz = target_hz, .tol_cents = tol_cents,
    };
    xQueueSend(s_job_q, &job, 0);
}

// Cancel must bypass the job queue: the worker is busy inside run_sing and
// only checks s_cancel between frames. Direct volatile write from UI context.
void ear_audio_sing_cancel(void) {
    s_cancel = true;
}

// Apply a new volume (0-100) for subsequent jobs; also live-applies to the
// codec when idle-safe (next job start re-applies anyway).
void ear_audio_set_volume(uint8_t vol) {
    s_volume = vol;
}
