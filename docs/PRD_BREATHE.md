<p align="right"><a href="PRD_BREATHE.zh_CN.md">简体中文</a> | English</p>

# BREATHE - Pocket Breathing Coach

> Status: Ready for firmware implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/breathe (from upstream main, post PR #107)
> PRD version: v1.1 (2026-10-16: stop-fade kills the OK-long pop, chime
> zero-crossing start + edge ramps, default volume 60->70; v1.0 initial)
> Mode: creator-selected new standalone play (breathing companion); tone
> renderer lineage: ear_tone.c integer wavetable (device-proven 2026-09-29).

## 1. Product Overview

- UI name: BREATHE - a pocket guided-breathing companion
- Fully offline; no network, no account, no microphone
- Three guided patterns + adjustable volume; lifetime minutes/cycles/sessions
- Eyes-free by design: a soft tone glides UP while breathing in and DOWN
  while breathing out; holds fade to silence - you can follow with eyes closed

## 2. Hardware Facts

- Same board baseline as the repository guide; battery % top-right
  (bsp_battery_soc(), -1 -> "--%"); ES8311 playback at 16 kHz mono
- Montserrat 14/20/48 (48 added to sdkconfig.defaults for the big
  countdown/value readouts, same as feature/tuner; UI text is ASCII
  English - no Chinese glyphs in the built-in fonts)
- No RTC -> no dates, no streaks; persistence is lifetime counters only
- Continuous-playback feed-latency rule (hardware guide 8.1): NVS writes and
  volume changes happen only outside playback; DMA budget is 90 ms and the
  integer synth keeps >10x realtime render headroom

## 3. Patterns (v1)

| Pattern | Inhale | Hold | Exhale | Hold2 | Cycle | Feel |
| --- | --- | --- | --- | --- | --- | --- |
| FLOW | 5.5 s | - | 5.5 s | - | 11 s | coherent breathing, ~5.5 breaths/min |
| CALM | 4 s | 7 s | 8 s | - | 19 s | 4-7-8 relax |
| BOX | 4 s | 4 s | 4 s | 4 s | 16 s | box breathing, focus |

Zero-length phases are skipped, not rendered as phases.

## 4. Behavior, Screens & Buttons

- HOME: title "BREATHE", battery top-right; rows FLOW / CALM / BOX / VOLUME
  with sub-lines (phase durations / current volume %); footer lifetime stats
  "N min - N cycles". UP/DOWN move the selection (wrap), OK enters. 10 s
  idle dims to 30% (repo power convention); any key wakes.
- VOLUME: big value + bar; UP/DOWN step ±10 (0..100, default 70), OK returns
  to HOME (persisted).
- SESSION: 3 s "GET READY" countdown with a soft tick each second, then
  repeating cycles: the center circle grows on INHALE and shrinks on EXHALE;
  phase word + per-phase seconds countdown; cycle counter and elapsed M:SS.
  The backlight stays on during a session; OK long-press ends it (soft
  rising chime) and opens SUMMARY.
- SUMMARY: this session (cycles, minutes) plus lifetime totals; stats are
  persisted here, once, after playback has stopped; OK returns HOME.
- Audio guidance: inhale = soft glide ~G3->D4 (196->294 Hz), exhale reversed,
  holds fade to silence; end chime E5->A5; volume follows the setting.
  Glide range A3->E4 (220->330 Hz).

## 5. Technical Architecture

```text
main/
  breath.h           - shared constants/types ([PURE] header)
  breath_engine.c/.h - [PURE] pattern tables + session timeline: elapsed ms
                      -> phase / progress / remaining; zero-length holds
                      skip; cycle & minute accumulation
  breath_tone.c/.h   - [PURE] integer wavetable glide renderer (LUT approach
                      ported from feature/ear-buddy ear_tone.c): linear
                      frequency glide via per-sample phase-step interpolation,
                      attack/release envelope, fade-out arm for holds
  breath_audio.c/.h  - worker task owning the session clock (sample count ->
                      elapsed ms): renders 512-sample chunks, re-arms glides
                      at sample-exact phase boundaries, posts 10 Hz UI
                      events, plays the end chime; all bsp_audio calls
                      serialize here
  breath_store.c/.h  - NVS namespace "breathe": vol/ssn/min/cyc; failures
                      degrade to defaults, log only
  breath_ui.c/.h     - LVGL: HOME / VOLUME / SESSION / SUMMARY screens +
                      idle dim + battery refresh
  main.c             - BSP init -> event queues -> input/audio dispatch
                      (key_guard wiring mandatory; pattern per feature/tuner)
tests/test_breath_engine.c, tests/test_breath_tone.c
```

[PURE] modules: zero IDF/LVGL deps; compile with cc -std=c11 -Wall -Wextra
-Werror; registered in tools/validate.sh.

Timing ownership: the audio worker's sample counter is the single session
clock; the UI only renders the 10 Hz events it posts (no second clock to
drift).

## 6. Risks & AI Default Decisions

| Decision | Value | Rationale |
| --- | --- | --- |
| Glide range 220-330 Hz | a fourth up/down (A3->E4) | clearly directional, calm, small-speaker friendly |
| Volume default 70 | balanced | calmer than 80, still audible on the tiny speaker; user-adjustable |
| No pause in v1 | OK long ends the session | a pause complicates the timeline; guided breathing rarely pauses |
| No session length presets | breathe as long as liked | wellness-friendly; fewer screens |
| No streaks | no RTC | honest lifetime counters only |
| NVS only outside playback | guide 8.1 | no crackle risk from flash stalls |
| Risk: tone audibility at volume 70 on the tiny speaker | device check v1.1 | direction clarity confirmed on device 2026-10-16 |

## 7. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately. Device
checklist: pattern timing feel, glide audibility and direction clarity,
volume range, dim behavior, stats persistence across reboot.
