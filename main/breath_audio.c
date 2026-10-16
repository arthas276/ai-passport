// breath_audio.c - single audio worker for BREATHE: owns the session clock.
//
// The worker's written-sample counter is the ONLY session clock: engine
// phases are evaluated from it at chunk starts, so audio and the 10 Hz UI
// events can never drift apart. Phase re-arms land on chunk boundaries, so
// a tone glide starts within one 512-sample chunk (32 ms) of its true
// boundary - imperceptible next to 4-5.5 s phases, and never cumulative
// (every arm re-syncs at the next chunk boundary).
//
// Feed-latency rule (hardware guide 8.1): this worker never writes NVS and
// never changes format mid-session; DMA budget is 90 ms and the integer
// synth renders a 512-sample chunk in well under a millisecond.
#include "breath_audio.h"
#include "breath_tone.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "breath_audio";

#define CHUNK 512
#define TICK_SAMPLES (BREATH_TONE_SR * BREATH_TICK_MS / 1000u)   // 1600
#define MS_TO_SAMP(ms) ((uint32_t)(ms) * (BREATH_TONE_SR / 1000u))
#define READY_TICK_MS 60
#define STOP_FADE_SAMPLES 640u   // 40 ms release when the user ends mid-glide

typedef struct { uint8_t cmd; uint8_t pattern; } breath_job_t;
#define CMD_START 1
#define CMD_STOP  2

static QueueHandle_t s_job_q;
static QueueHandle_t s_event_q;
static TaskHandle_t s_task;
static volatile uint8_t s_volume = 70;

// Rising two-note end chime (E5 -> A5), integer triangle wave - the same
// cue shape feature/tuner used, which is clearly audible on this speaker.
// Starts at the wave's zero crossing and carries ~6 ms linear edges so
// neither note seam steps between silence and full amplitude (a hard
// start/end at ~9k amplitude clicks).
static void play_tri_note(float hz, int ms, int32_t scale) {
    static int16_t buf[CHUNK];
    int32_t step = (int32_t)(hz * 1024.0f / (float)BREATH_TONE_SR);
    int32_t phase = 256 << 10;      // zero crossing of the triangle
    const int total = (int)BREATH_TONE_SR * ms / 1000;
    const int ramp = 96;            // ~6 ms edge
    for (int pos = 0; pos < total;) {
        int n = total - pos < CHUNK ? total - pos : CHUNK;
        for (int i = 0; i < n; i++) {
            int idx = pos + i;
            int32_t r = idx < ramp ? idx
                      : (total - idx < ramp ? total - idx : ramp);
            int32_t ph = (phase >> 10) & 1023;
            int32_t tri = ph < 512 ? ph * 2 - 512 : 1536 - ph * 2;
            buf[i] = (int16_t)(tri * scale * r / ramp);
            phase += step;
        }
        if (bsp_audio_write(buf, n * 2) != ESP_OK) return;
        pos += n;
    }
}

static void play_gap(int ms) {
    static int16_t buf[CHUNK];
    memset(buf, 0, sizeof(buf));
    int left = (int)BREATH_TONE_SR * ms / 1000;
    while (left > 0) {
        int n = left < CHUNK ? left : CHUNK;
        bsp_audio_write(buf, n * 2);
        left -= n;
    }
}

static void post_event(breath_audio_ev_kind_t kind, const breath_state_t *st) {
    breath_audio_event_t ev = { .kind = kind, .st = *st };
    xQueueSend(s_event_q, &ev, 0);
}

static void run_session(uint8_t pattern_idx) {
    const breath_pattern_t *p = &BREATH_PATTERNS[pattern_idx];
    static int16_t buf[CHUNK];
    breath_tone_t tone;
    breath_state_t st = {0};
    breath_phase_t cur = BREATH_PHASE_READY;
    bool tone_armed = false;
    uint16_t prev_ready_left = BREATH_READY_MS;
    uint32_t written = 0;      // session samples pushed to DMA (the clock)
    uint32_t next_tick = 0;    // sample position of the next UI tick

    if (bsp_audio_set_format(BREATH_TONE_SR, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "set_format failed; session aborted");
        post_event(BREATH_AUDIO_EV_DONE, &st);
        return;
    }
    bsp_audio_set_volume(s_volume);

    for (;;) {
        breath_job_t job;
        if (xQueueReceive(s_job_q, &job, 0) == pdTRUE) {
            if (job.cmd == CMD_STOP) break;
            // START mid-session violates the UI contract; ignore it rather
            // than restart a stream mid-glide.
        }

        breath_engine_at(p, written / (BREATH_TONE_SR / 1000u), &st);

        if (st.phase != cur) {
            cur = st.phase;
            tone_armed = false;
            if (cur == BREATH_PHASE_INHALE) {
                breath_tone_glide(&tone, BREATH_HZ_LOW, BREATH_HZ_HIGH,
                                  MS_TO_SAMP(p->inhale_ms), 0.9f);
                tone_armed = true;
            } else if (cur == BREATH_PHASE_EXHALE) {
                breath_tone_glide(&tone, BREATH_HZ_HIGH, BREATH_HZ_LOW,
                                  MS_TO_SAMP(p->exhale_ms), 0.9f);
                tone_armed = true;
            } else if (cur == BREATH_PHASE_READY) {
                // countdown starts now: first of the 3-2-1 ticks
                breath_tone_glide(&tone, 440.0f, 440.0f,
                                  MS_TO_SAMP(READY_TICK_MS), 0.35f);
                tone_armed = true;
                prev_ready_left = st.phase_left_ms;
            }
            // HOLD / HOLD2: silence (the glide release already faded out).
        } else if (cur == BREATH_PHASE_READY) {
            // one tick per remaining second: 3, 2, 1
            if ((prev_ready_left > 1000 && st.phase_left_ms <= 1000) ||
                (prev_ready_left > 2000 && st.phase_left_ms <= 2000)) {
                breath_tone_glide(&tone, 440.0f, 440.0f,
                                  MS_TO_SAMP(READY_TICK_MS), 0.35f);
                tone_armed = true;
            }
            prev_ready_left = st.phase_left_ms;
        }

        if (tone_armed && tone.t < tone.total) {
            breath_tone_render(&tone, buf, CHUNK);
        } else {
            memset(buf, 0, sizeof(buf));
        }
        if (bsp_audio_write(buf, CHUNK * 2) != ESP_OK) {
            ESP_LOGE(TAG, "playback write failed; ending session");
            break;
        }
        written += CHUNK;

        if (written >= next_tick) {
            post_event(BREATH_AUDIO_EV_TICK, &st);
            next_tick += TICK_SAMPLES;
        }
    }

    // Session over: fade the still-sounding glide out first (cutting a
    // sustained tone at full amplitude pops), then a short breath of
    // silence and the end chime; the DONE event carries the final state
    // (completed cycles, true elapsed).
    if (tone_armed && tone.t < tone.total) {
        breath_tone_fade_tail(&tone, STOP_FADE_SAMPLES);
        int left = (int)(tone.total - tone.t);
        while (left > 0) {
            int n = left < CHUNK ? left : CHUNK;
            breath_tone_render(&tone, buf, (uint16_t)n);
            if (bsp_audio_write(buf, n * 2) != ESP_OK) break;
            left -= n;
        }
    }
    play_gap(150);
    play_tri_note(659.26f, 130, 18);
    play_gap(25);
    play_tri_note(880.0f, 130, 18);
    breath_engine_at(p, written / (BREATH_TONE_SR / 1000u), &st);
    post_event(BREATH_AUDIO_EV_DONE, &st);
}

static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        breath_job_t job;
        if (xQueueReceive(s_job_q, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.cmd == CMD_START) run_session(job.pattern);
        // CMD_STOP with no session running is a no-op.
    }
}

esp_err_t breath_audio_init(QueueHandle_t event_q) {
    if (s_task) return ESP_OK;   // idempotent
    s_event_q = event_q;
    s_job_q = xQueueCreate(4, sizeof(breath_job_t));
    if (!s_job_q) return ESP_ERR_NO_MEM;
    esp_err_t bsp = bsp_audio_init();
    if (bsp != ESP_OK) {
        // Keep going: the worker still runs and the first session will
        // fail loudly through the event flow.
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(bsp));
    }
    if (xTaskCreate(audio_task, "breath_audio", 6144, NULL, 4, &s_task)
        != pdPASS) {
        vQueueDelete(s_job_q);
        s_job_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void breath_audio_session_start(uint8_t pattern_idx) {
    if (pattern_idx >= BREATH_PATTERN_COUNT) return;
    breath_job_t job = { .cmd = CMD_START, .pattern = pattern_idx };
    xQueueSend(s_job_q, &job, 0);
}

void breath_audio_session_stop(void) {
    breath_job_t job = { .cmd = CMD_STOP };
    xQueueSend(s_job_q, &job, 0);
}

void breath_audio_set_volume(uint8_t percent) {
    s_volume = percent > 100 ? 100 : percent;
}
