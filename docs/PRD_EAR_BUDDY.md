<p align="right"><a href="PRD_EAR_BUDDY.zh_CN.md">简体中文</a> | English</p>

# EAR BUDDY - Product Requirements Document

> Status: Ready for firmware implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/ear-buddy (from main)
> PRD version: v1.0
> Date: 2026-09-28
> Mode: user pre-authorized autonomous overnight build; AI made the listed
> default decisions; on-device verification planned next evening with user.

## 1. Product Overview

- UI name: EAR BUDDY — pocket ear-training companion + light pet-raising meta
- Internal prefix: ear_
- Fully offline; no network, no account; progress in NVS
- Proven by user: xiaozhi.me firmware runs on this hardware (Wi-Fi + mic +
  speaker verified working on the same device)
- Background: user first considered NFC door-card emulation — not possible
  (NTAG213 is a passive tag with no MCU interface); audio direction chosen;
  user picked 3 modes: interval+chord quizzes, sing-in-tune, pet growth

## 2. Hardware Facts (bsp_pins.h / specs / AGENTS)

- ESP32-C3, 8 MB flash, no PSRAM, LV_MEM 24 KB -> lean UI, no fullscreen anims
- No RTC time source -> no date/streak features in MVP (same as pomodoro PRD)
- sdkconfig enables Montserrat 14/20 only -> MVP UI in English; Chinese UI
  needs lv_font_conv (node.js), deferred, listed under Unverified
- ES8311: sample-rate switching via bsp_audio_set_format (close/open inside);
  blocking PCM; all audio ops serialize on one worker task
- Battery top-right per repo default convention (bsp_battery_soc(), -1 -> --%)
- 10 s idle -> backlight 30%, any key restores 100% (repo power convention)

## 3. Modes & Rules

### 3.1 Quiz (Interval / Chord)

- Session = 10 questions; roots C4-A4; ascending only (desc/harmonic deferred)
- Option sets by level:
  - INTERVAL: L1 {m3, M3}; L2 {m3, M3, P4, P5}; L3 {m2, M2, m3, M3, P4, P5}
  - CHORD: L1 {maj, min}; L2 {maj, min, dim, aug}; L3 {maj, min, dim, aug, M7, 7}
- Answer flow: pick with UP/DOWN, confirm with OK; correct -> +1 XP, pet hop
  + happy blip; wrong -> correct option highlighted, correct answer replayed,
  next question; no XP loss
- Auto level per mode: session score >= 8 -> level up (max 3); <= 4 -> down
  (min 1); persisted in NVS
- Combo counter display-only (no XP effect)
- Tone: additive synthesis (f + 0.5*2f + 0.25*3f), fast attack, exp decay,
  22050 Hz 16-bit mono; interval = two notes 450 ms + 90 ms gap; chord = three
  notes together 900 ms; roots C4-A4 (261.6-440 Hz, tiny-speaker friendly)

### 3.2 Sing mode

- Play target note (C4-A4), show note name, UP replays target, OK starts
  listening (max 3 s)
- 16 kHz capture -> 1024-pt autocorrelation f0 (65-600 Hz) -> median-3 ->
  cents = 1200*log2(f0/f_target)
- PASS if |cents| <= tolerance held for 1.5 s within the 3 s window;
  tolerance L1 +/-50, L2 +/-35, L3 +/-25 cents; PASS -> +2 XP
- 3 failed attempts -> device plays the target note (show answer) -> next
  note; a sing session = 5 notes
- Playback (22050 Hz) and capture (16 kHz) switch via bsp_audio_set_format;
  serialized on the single audio worker (BSP constraint)

## 4. Pet Economy

- Stages (cumulative XP): Egg 0 -> Hatchling 10 -> Chick 30 -> Fledgling 80
  -> Songbird 160
- XP sources: quiz correct +1; perfect session (10/10) +3; sing PASS +2/note
- Reactions: correct -> pet hop + happy blip; wrong -> head shake; evolve ->
  stage image swap + fanfare + hop x3
- XP never decreases (no-punishment principle); no dates, no streaks
- NVS read failure -> treat as new pet, rebuild archive, no crash

## 5. Screens & Button Semantics (redesigned UI, not the demo shell)

HOME
- top: "EAR BUDDY" title + battery % top-right
- center: pet sprite 48x48 + XP bar (180x10) + "LV n cur/next" text
- bottom: mode card cycling INTERVAL / CHORD / SING / SETUP via UP/DOWN
- UP/DOWN click: switch mode card; OK click: enter mode; OK long: nothing

QUIZ
- header "Q 3/10 LV2"; status "LISTEN..." during playback
- options grid: 2 / 4 / 6 cells (100x44 each), cursor highlight
- footer hints "OK CONFIRM - UPL REPLAY - OKL QUIT" (Montserrat 14)
- UP long: replay question; OK long: quit session (earned XP stays);
  combo >= 3 shown in header
- wrong answer: correct cell highlights 1.5 s with replay, then next

SING
- "SING: E4" big; horizontal cents scale -50..+50 with moving marker;
  hold progress bar; "OK START - UP REPLAY" hint
- OK starts 3 s attempt; live marker; PASS -> "+2 XP" flash -> next note
- 3 fails -> device plays target (show answer) -> next note

RESULT (after 10 quiz questions)
- "SCORE 8/10" + "+9 XP" + pet hop anim; OK -> HOME

SETUP
- rows: VOLUME (UP/DOWN +/-10, OK save+back); RESET PROGRESS (OK long ->
  reset, back to HOME; second-confirm page deferred)
- battery top-right

Global: 10 s idle -> backlight 30%; any key -> 100%. Implemented in ear_ui.

## 6. Button Map (uniform)

| Input | HOME | QUIZ | SING | RESULT | SETUP |
|---|---|---|---|---|---|
| UP/DOWN click | switch card | move cursor | UP=replay | - | select/adjust |
| OK click | enter mode | confirm answer | start attempt | to HOME | save+back |
| OK long | - | quit session | quit | - | RESET row: reset |
| UP long | - | replay audio | - | - | - |

OK-long quit never revokes earned XP (XP booked + NVS-written at the moment
of each correct answer / PASS).

## 7. Data & Persistence

NVS namespace earbuddy: xp u32; ilvl/clvl/slvl u8 (1-3); vol u8 (0-100).
Read failure -> defaults (new pet). Write on XP change (1-2x per session,
NVS wear is a non-issue) and on settings change. Write failure -> log only.

## 8. Technical Architecture

main/
  main.c            - app_main: bsp init -> ear boot -> input dispatch task
  ear_game.h        - shared types/constants (XP rules, stage thresholds,
                      note table C4-A4, level option tables, colors)
  ear_model.c/h     - [PURE] question gen / grading / level adjust / session
  ear_pet.c/h       - [PURE] XP->stage mapping / feed / evolve checks
  ear_pitch.c/h     - [PURE] autocorrelation f0 + interpolation + conf +
                      cents + note-name helper
  ear_tone.c/h      - [PURE] additive synth into int16 buffer (C4-A4 table)
  ear_store.c/h     - NVS load/save (namespace earbuddy)
  ear_audio.c/h     - audio worker task: play queue + mic->pitch->event queue
  ear_ui.c/h        - 5 LVGL pages + backlight idle manager
  ear_assets.c/h    - pet sprites 5 stages x 2 frames, 48x48 RGB565
                      descriptors (generated)
tools/gen_pet_assets.py - pixel-art generator (code-drawn egg/chick/bird ->
                      RGB565 C arrays)
tests/test_ear_model.c   - question uniqueness/grading/level boundaries
tests/test_ear_pet.c     - thresholds/XP monotonic/perfect bonus
tests/test_ear_pitch.c   - synthetic sine PCMs -> detected pitch within tol

[PURE] modules: zero IDF/LVGL deps; must compile clean with
gcc -std=c11 -Wall -Wextra -Werror. Registered in tools/validate.sh
(funbox precedent: validate.sh may be edited on feature branches).

## 9. Risks

| Risk | Mitigation |
|---|---|
| Speaker/mic quality unmeasured | xiaozhi proves link works; on-device A/V check tomorrow |
| Chinese UI deferred | needs lv_font_conv/node env; English MVP; tell user |
| Synth timbre may need tuning | params centralized in ear_tone.c for device tuning |
| No RTC -> no date features | same cut as pomodoro PRD; NTP/RTC later |
| LVGL 9.5 API drift | code against managed_components lvgl 9.5.0 headers |
| Sealed device, no expansion | pure software; zero new hardware deps |

## 10. AI Default Decisions (for user review tomorrow)

| Decision | Value | Rationale |
|---|---|---|
| UI language | English | no lv_font_conv/node env tonight |
| Timbre | synthesized | no licensed samples; params centralized |
| Range | C4-A4 | tiny-speaker friendly + standard ear-training range |
| Wrong answers | no penalty | no-punishment principle |
| Stage thresholds | 10/30/80/160 XP | ~2-3 weeks daily practice to max |
| Sing tolerance | +/-50/35/25 cents | L1 forgiving, L3 challenging |
| 3 fails -> show answer | yes | avoid stuck states |
| OK-long quit keeps XP | yes | no-punishment principle |
| Idle backlight 30% after 10 s | yes | repo power convention |
| RESET = OK long on row | yes | second-confirm page deferred |

## 11. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately.
Flashing + on-device acceptance tomorrow with user consent. Tonight =
code + PRD + host tests + firmware build only.
