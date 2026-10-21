<p align="right"><a href="PRD_TUNER.zh_CN.md">简体中文</a> | English</p>

# TUNER - Pocket Chromatic Tuner

> Status: Ready for firmware implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/tuner (from upstream main)
> PRD version: v1.2 (2026-10-21: hold UP in instrument modes plays the current target string's reference tone ~1.2 s, capture pauses while it plays; v1.1 2026-10-11: GUITAR/UKULELE/VIOLIN string modes with lock-to-next-string, VOICE mode with session range, OK-long mode cycling; v1.0 chromatic core)
> Date: 2026-10-11
> Mode: user-authorized autonomous build; pitch detector ported from
> feature/ear-buddy ear_pitch.c (same repository, device-proven 2026-09-29).

## 1. Product Overview

- UI name: TUNER — a wearable chromatic tuner
- Fully offline; no network, no account, no persistence (no settings in v1)
- Detects any sustained pitch from low guitar E2 (82.4 Hz) to ~1000 Hz,
  shows the nearest note name, octave, frequency, and a +/-50 cent needle
- A green lock zone (+/-5 cents) with an audible chime when the note holds

## 2. Hardware Facts (bsp_pins.h / specs / AGENTS)

- Same board baseline as the repository guide; battery top-right convention
  (bsp_battery_soc(), -1 -> "--%"); ES8311 capture+playback at 16 kHz mono
- Montserrat 14/20 enabled upstream; v1 adds Montserrat 48 in
  sdkconfig.defaults for the big note readout (English-only UI, no lv_font_conv)
- No RTC, no persistence features in v1

## 3. Behavior, Screen & Buttons

Single screen (redesigned UI, not the demo shell) with five modes cycled by
holding OK: CHROMATIC (default), GUITAR (E2 A2 D3 G3 B3 E4), UKULELE
(G4 C4 E4 A4, reentrant), VIOLIN (G3 D4 A4 E5), and VOICE.

- Top: "TUNER • <MODE>" title; battery % top-right
- Center: the note name at Montserrat 48 - the current target string in
  instrument modes, the nearest note otherwise; "--" when unvoiced.
  Aux line: "STR n/6" in instrument modes, "RANGE C3-A4" in VOICE (session
  low/high of every voiced frame). Frequency in Hz below at Montserrat 20
- Needle bar 200 px wide, +/-50 cents around the reference (target string
  or nearest note), green +/-5 cent zone, moving marker clamped at the
  edges; status word LOW / HIGH / IN TUNE
- Lock: 8 consecutive in-tune frames -> status IN TUNE (green) + one chime;
  instrument modes then auto-advance to the next string (wraparound)
- OK click freezes/resumes the readout (label shows FROZEN); OK long cycles
  the mode (resets strings/range); UP/DOWN steps the target string in
  instrument modes (hold UP plays the current target string's reference tone, ~1.2 s, capture pauses while it plays); any key wakes the backlight; 10 s idle dims to 30%
  (repo power convention)

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
  main.c            - BSP init -> input/audio dispatch task
tests/test_tuner_pitch.c - sine sweep across range, low-E2 detection,
                      octave-safety, median-3, note naming, gates
tests/test_tuner_modes.c  - mode tables, cycle order, string stepping,
                      voice-range tracking

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
| No settings/NVS | none needed | volume fixed at 80; mode resets at boot |
| Risk: low-string detection in noise | synthetic tests + device check | lowest period ~229 samples vs 1024 frame |

## 6. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately. Device
check needs a real sustained instrument note (user-provided).
