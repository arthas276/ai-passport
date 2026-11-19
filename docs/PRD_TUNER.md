<p align="right"><a href="PRD_TUNER.zh_CN.md">简体中文</a> | English</p>

# TUNER - Pocket Chromatic Tuner

> Status: Ready for firmware implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/tuner (from upstream main)
> PRD version: v1.3 (2026-11-19: Chinese UI labels from a generated glyph-audited WenKai 20 subset; settings screen on hold-DOWN with A4 reference 415-466 Hz, written-pitch transpose OFF/Bb/Eb/F, speaker volume; NVS persistence of mode/volume/A4/transpose; string-progress dots, EMA needle smoothing, all-strings-done status; v1.2 2026-10-21: hold UP in instrument modes plays the current target string's reference tone ~1.2 s, capture pauses while it plays; v1.1 2026-10-11: GUITAR/UKULELE/VIOLIN string modes with lock-to-next-string, VOICE mode with session range, OK-long mode cycling; v1.0 chromatic core)
> Date: 2026-11-19
> Mode: user-authorized autonomous build; pitch detector ported from
> feature/ear-buddy ear_pitch.c (same repository, device-proven 2026-09-29).

## 1. Product Overview

- UI name: TUNER — a wearable chromatic tuner
- Fully offline; no network, no account. Since v1.3 four UI settings
  (mode, volume, A4 reference, transpose) persist in NVS
- Detects any sustained pitch from low guitar E2 (82.4 Hz) to ~1000 Hz,
  shows the nearest note name, octave, frequency, and a +/-50 cent needle
- A green lock zone (+/-5 cents) with an audible chime when the note holds

## 2. Hardware Facts (bsp_pins.h / specs / AGENTS)

- Same board baseline as the repository guide; battery top-right convention
  (bsp_battery_soc(), -1 -> "--%"); ES8311 capture+playback at 16 kHz mono
- Montserrat 14/20 enabled upstream; v1 adds Montserrat 48 in
  sdkconfig.defaults for the big note readout. Since v1.3 the Chinese UI
  labels (title, aux line, status, hints, settings rows) render in a
  generated 20 px WenKai subset (tools/tuner/gen_assets.mjs, glyph-audited;
  the on-screen Chinese wording lives in PRD_TUNER.zh_CN.md); note names,
  the Hz readout and the battery stay Montserrat ASCII
- No RTC, no persistence features in v1

## 3. Behavior, Screen & Buttons

Single screen (redesigned UI, not the demo shell) with five modes cycled by
holding OK: CHROMATIC (default), GUITAR (E2 A2 D3 G3 B3 E4), UKULELE
(G4 C4 E4 A4, reentrant), VIOLIN (G3 D4 A4 E5), and VOICE.

- Top: bare mode-name title (Chinese since v1.3); battery % top-right
- Center: the note name at Montserrat 48 - the current target string in
  instrument modes, the nearest note otherwise; "--" when unvoiced.
  Since v1.3 free modes (CHROMATIC/VOICE) show the WRITTEN pitch when
  transpose is active (sounding + offset); instrument targets always show
  sounding pitch, and the VOICE range line transposes with the setting.
  Aux line: string index in instrument modes, session low/high in VOICE
  (every voiced frame). Frequency in Hz below at Montserrat 20
- Needle bar 200 px wide, +/-50 cents around the reference (target string
  or nearest note), green +/-5 cent zone, moving marker clamped at the
  edges and EMA-smoothed since v1.3 (factor 0.35, reset on unvoiced or
  string change; lock judgement stays on raw cents); status word is the
  Chinese low/high/in-tune label
- Lock: 8 consecutive in-tune frames -> in-tune status (green) + one chime;
  instrument modes then auto-advance to the next string (wraparound).
  v1.3: a per-string done bit lights a progress dot row (done green /
  current accent / pending edge); every string done shows an all-done status
- OK click freezes/resumes the readout (frozen hint shows); OK long cycles
  the mode (resets strings/range/progress); UP/DOWN steps the target string
  in instrument modes (hold UP plays the current target string's reference
  tone, ~1.2 s, capture pauses while it plays); hold DOWN opens the
  settings screen; any key wakes the backlight; 10 s idle dims to 30%
  (repo power convention). The bottom hint line rotates every 2.5 s so all
  gestures stay advertised within the 240 px width
- Settings screen (v1.3): three rows - A4 reference (415-466 Hz, OK steps
  +1 with wraparound), transpose (OFF/Bb/Eb/F), volume (0-100, OK steps
  +10 with wraparound); UP/DOWN selects the row, OK adjusts, OK long saves
  everything (current mode included) to NVS and returns. A4 recalibrates
  note naming, reference tones and the green zone together; defaults
  440 Hz / 80 / OFF / CHROMATIC restore v1.2 behaviour exactly

## 4. Technical Architecture

main/
  tuner.h           - shared constants/types ([PURE] header)
  tuner_pitch.c/.h  - [PURE] autocorrelation detector ported from
                      feature/ear-buddy ear_pitch.c: RMS gate, earliest-peak
                      + integer-divisor octave safety, parabolic
                      interpolation, median-3; full-range note naming
  tuner_modes.c/.h - [PURE] mode tables (string tunings), string stepper
                      with lock advance, voice-range tracker
  tuner_audio.c/.h - worker: continuous 16 kHz capture -> detect -> event
                      queue; serial cue playback (chime) between frames
  tuner_ui.c/.h     - single LVGL screen + idle backlight manager
  main.c            - BSP init -> settings load/apply -> input/audio dispatch
  tuner_settings.c/.h - [PURE] settings model: defaults, sanitisation,
                      transpose table
  tuner_store.c/.h  - NVS persistence (4 scalar keys; missing -> defaults)
  tuner_font_20.c   - generated WenKai 20 subset (tuner_strings.h literals)
tests/test_tuner_pitch.c - sine sweep across range, low-E2 detection,
                      octave-safety, median-3, note naming, gates; v1.3:
                      A4 calibration math
tests/test_tuner_modes.c  - mode tables, cycle order, string stepping,
                      voice-range tracking; v1.3: string-progress mask,
                      transpose, EMA
tests/test_tuner_settings.c - defaults, sanitisation, transpose cycling
tools/tuner/gen_assets.mjs - font subset generator + glyph audit

[PURE] modules: zero IDF/LVGL deps; compile with cc -std=c11 -Wall -Wextra
-Werror; registered in tools/validate.sh.

## 5. Risks & AI Default Decisions

| Decision | Value | Rationale |
| --- | --- | --- |
| Range 70-1000 Hz | covers E2..B5 | guitar/bass-low-E to violin E5 |
| Green zone +/-5 cents | tight but visible | standard tuner display |
| Lock = 8 frames (~0.5 s) | stable, not sluggish | 64 ms frames |
| No string-target markers | v1.1 adds instrument modes | guitar/ukulele/violin strings with auto-advance |
| No bass mode | v1.1 out of scope | low E1 41 Hz sits under the 70 Hz floor; needs a longer frame |
| Settings in NVS (v1.3) | 4 scalar keys | mode/volume/A4/transpose; missing keys take defaults (CHROMATIC/80/440/OFF); no blob, no migration needed |
| Risk: low-string detection in noise | synthetic tests + device check | lowest period ~229 samples vs 1024 frame |

## 6. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately. Device
check needs a real sustained instrument note (user-provided).
