<p align="right"><a href="PRD_BREATHE.zh_CN.md">简体中文</a> | English</p>

# BREATHE - Pocket Breathing Coach

> Status: Ready for firmware implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/breathe (from upstream main, post PR #107)
> PRD version: v1.3 (2026-11-13: your-pace pass - SIGH physiological
> sigh preset (2.5 s in + 1.5 s top-up + 7 s out; engine gains a fifth
> phase INHALE2, tone glides A3->E4 then E4->G4 on the top-up and falls
> G4->A3 on the exhale, screen word "TOP UP"); YOURS custom pattern
> (seventh slot; SETUP->YOURS opens the EDIT screen; in/hold/out/hold2
> 1-10 s / 0-10 s adjusted per row with wrap, OK advances rows, OK on the
> last row saves, long-press cancels; NVS keys ci/ch/co/c2; live sub on
> HOME and SETUP like "4 + 2 + 6 + 2"; HOME relaid out as 8 rows at a
> 33 px pitch); v1.2 (2026-11-02: sleep-ready pass - SLEEP (4+6 extended
> exhale) and WAKE (6+3 inhale-dominant) patterns; timed sessions (FREE /
> 1 / 3 / 5 minutes, ending on the first completed cycle boundary after the
> target); GLOW setting (BRIGHT / NIGHT 12% / DARK audio-only, session
> backlight settles to the level after 10 s idle, any key peeks back);
> lifetime-minute milestones celebrated on SUMMARY (10/30/60/120/300/600,
> then doubling); SETUP screen holds DURATION / GLOW / VOLUME with rounded
> hint pills on every screen and a frozen-glyph 204 px text budget enforced
> by tests/test_hint_budget.py; session pace readout on SUMMARY); v1.1
> (2026-10-16: stop-fade kills the OK-long pop, chime
> zero-crossing start + edge ramps, default volume 60->70; v1.0 initial)
> Mode: creator-selected new standalone play (breathing companion); tone
> renderer lineage: ear_tone.c integer wavetable (device-proven 2026-09-29).

## 1. Product Overview

- UI name: BREATHE - a pocket guided-breathing companion
- Fully offline; no network, no account, no microphone
- Seven guided rhythms (six presets + YOURS custom) + adjustable volume;
  lifetime minutes/cycles/sessions
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

## 3. Patterns (v1.3)

| Pattern | Inhale | Hold | Top-up | Exhale | Hold2 | Cycle | Feel |
| --- | --- | --- | --- | --- | --- | --- | --- |
| FLOW | 5.5 s | - | - | 5.5 s | - | 11 s | coherent breathing, ~5.5 breaths/min |
| CALM | 4 s | 7 s | - | 8 s | - | 19 s | 4-7-8 relax |
| SIGH | 2.5 s | - | 1.5 s | 7 s | - | 11 s | physiological sigh: double inhale + long exhale (v1.3) |
| BOX | 4 s | 4 s | - | 4 s | 4 s | 16 s | box breathing, focus |
| SLEEP | 4 s | - | - | 6 s | - | 10 s | extended exhale, wind-down (v1.2) |
| WAKE | 6 s | - | - | 3 s | - | 9 s | inhale-dominant, morning (v1.2) |
| YOURS | 1-10 s | 0-10 s | - | 1-10 s | 0-10 s | custom | user-defined rhythm, NVS-persisted (v1.3, default 5+5) |

Zero-length phases are skipped, not rendered as phases. Phase order:
inhale -> hold -> top-up (INHALE2, SIGH preset only) -> exhale -> hold2.

## 4. Behavior, Screens & Buttons

- HOME: title "BREATHE", battery top-right; rows FLOW / CALM / SIGH /
  BOX / SLEEP / WAKE / YOURS / SETUP (v1.3: 8 rows at a 33 px pitch) with
  sub-lines (phase durations / live YOURS summary); footer lifetime stats
  "N min - N cycles". UP/DOWN move the selection (wrap), OK enters (YOURS
  starts like any preset). 10 s idle dims to 30% (repo power
  convention); any key wakes.
- SETUP (v1.3): rows DURATION (FREE / 1 / 3 / 5 MIN, OK cycles), GLOW
  (BRIGHT / NIGHT / DARK, OK cycles), VOLUME (OK opens the big screen),
  YOURS (live sub shows the custom summary, OK opens EDIT);
  OK long-press returns HOME. VOLUME/EDIT keep their screens and return to
  SETUP.
- EDIT (v1.3): YOURS pace editor, rows IN / HOLD / OUT / HOLD2; UP/DOWN
  adjust the selected row's seconds (in/out 1-10, holds 0-10, wrapping so
  no key is a silent no-op; the row's sub shows the live "N s"); OK moves
  to the next row, OK on the last row (HOLD2) saves and returns to SETUP;
  OK long-press cancels (discards edits). The pill switches with the
  selection: "UP/DOWN: SEC  OK: NEXT" / "OK: SAVE  LONG: CANCEL".
- SESSION: 3 s "GET READY" countdown with a soft tick each second, then
  repeating cycles: the center circle grows on INHALE and shrinks on EXHALE
  (a SIGH top-up resumes growth from three quarters, phase word
  "TOP UP"); phase word + per-phase seconds countdown; cycle counter and
  elapsed M:SS
  (timed sessions count down the remaining time instead). Backlight starts
  at 100%; with GLOW NIGHT/DARK it settles to 12% / 0% after 10 s idle and
  any key peeks back to full. OK long-press ends it (soft rising chime) and
  opens SUMMARY. A timed session ends by itself at the first inhale start
  after the guided target - never cutting a breath cycle in half.
- SUMMARY: this session (cycles, time, pace in cycles/min when >=1 min)
  plus lifetime totals and, when crossed, the milestone line
  "MILESTONE! N MIN"; stats are persisted here, once, after playback has
  stopped; OK returns HOME.
- Audio guidance: inhale = soft glide ~G3->D4 (196->294 Hz), exhale reversed,
  holds fade to silence; end chime E5->A5; volume follows the setting.
  Glide range A3->E4 (220->330 Hz); a SIGH top-up glides E4->G4
  (330->392 Hz) and patterns with a top-up exhale from G4 down to A3.

## 5. Technical Architecture

```text
main/
  breath.h           - shared constants/types ([PURE] header)
  breath_engine.c/.h - [PURE] pattern tables + session timeline: elapsed ms
                      -> phase / progress / remaining; zero-length holds
                      skip; cycle & minute accumulation; v1.3 runtime
                      table (init / pattern accessor / set_custom
                      validated write into the YOURS slot; sessions
                      snapshot the struct against edit races)
  breath_tone.c/.h   - [PURE] integer wavetable glide renderer (LUT approach
                      ported from feature/ear-buddy ear_tone.c): linear
                      frequency glide via per-sample phase-step interpolation,
                      attack/release envelope, fade-out arm for holds
  breath_audio.c/.h  - worker task owning the session clock (sample count ->
                      elapsed ms): renders 512-sample chunks, re-arms glides
                      at sample-exact phase boundaries, posts 10 Hz UI
                      events, plays the end chime; all bsp_audio calls
                      serialize here
  breath_store.c/.h  - NVS namespace "breathe": vol/du/gl +
                      ci/ch/co/c2 (YOURS seconds, v1.3; a partial or
                      corrupt set falls back to the 5+5 default) plus
                      ssn/min/cyc/ms; failures degrade to defaults, log only
  breath_ui.c/.h     - LVGL: HOME / SETUP / VOLUME / EDIT / SESSION /
                      SUMMARY screens + idle dim + battery refresh
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
| YOURS bounds 1-10 / 0-10 s | edit-time wrap + engine validation | in-bounds values are always legal cycles (in/out non-zero); out-of-range NVS falls back to defaults |
| Session-start pattern snapshot | runtime slot editable from SETTINGS | the snapshot makes the two invisible to each other, no locking |
| NVS only outside playback | guide 8.1 | no crackle risk from flash stalls |
| Risk: tone audibility at volume 70 on the tiny speaker | device check v1.1 | direction clarity confirmed on device 2026-10-16 |

## 7. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately. Device
checklist: pattern timing feel, glide audibility and direction clarity,
volume range, dim behavior, stats persistence across reboot.
