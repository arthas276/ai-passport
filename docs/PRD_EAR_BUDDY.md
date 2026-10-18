<p align="right"><a href="PRD_EAR_BUDDY.zh_CN.md">简体中文</a> | English</p>

# EAR BUDDY - Product Requirements Document

> Status: Ready for firmware implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/ear-buddy (from main)
> PRD version: v1.6 (2026-10-18: interval quiz gains descending and harmonic
> directions - DOWN mixes in at L2, HARMONIC at L3; grading and level rules
> unchanged); v1.5 (2026-10-18: reset moved to a full-screen confirm page -
> first hold enters, a second hold within 5 s executes, any key cancels);
> v1.4 (2026-10-17: DICTATION mode — hear a melody, sing it back; see 3.4)
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
- Battery top-right per repo default convention (bsp_battery_soc(), -1 -> --%);
  SETUP also shows SOC% + cell voltage (v1.1) because the CW2017 profile is
  provisional and not yet characterized against the real cell
- 10 s idle -> backlight 30%, any key restores 100% (repo power convention)

## 3. Modes & Rules

### 3.1 Quiz (Interval / Chord)

- Session = 10 questions; roots C4-A4; direction by level (v1.6): L1
  ascending only, L2 mixes ascending/descending, L3 mixes asc/desc/harmonic;
  the answer is the interval itself, direction-independent
- Option sets by level:
  - INTERVAL: L1 {m3, M3}; L2 {m3, M3, P4, P5}; L3 {m2, M2, m3, M3, P4, P5}
  - CHORD: L1 {maj, min}; L2 {maj, min, dim, aug}; L3 {maj, min, dim, aug, M7, 7}
- Answer flow: pick with UP/DOWN, confirm with OK; correct -> +1 XP, pet hop
  + happy blip; wrong -> correct option highlighted, correct answer replayed,
  next question; no XP loss
- Auto level per mode: session score >= 8 -> level up (max 3); <= 4 -> down
  (min 1); persisted in NVS
- Combo counter display-only (no XP effect)
- Tone: additive synthesis (f + 0.40*2f + 0.18*3f, v1.3 re-tint), fast
  attack, gentle decay to ~64% + 8 ms release, 16000 Hz 16-bit mono
  (board-validated rate; 22050 distorts); interval = two notes 650 ms +
  120 ms gap (descending = higher note first, same timings; harmonic = both
  notes together 1250 ms via the chord mixer); chord = three notes
  together 1250 ms; sing target preview
  950 ms (v1.3: longer, fuller per user request); roots C4-A4 (261.6-440
  Hz, tiny-speaker friendly)

### 3.2 Sing mode

- Play target note (C4-A4), show note name, UP replays target, OK starts
  listening (max 4 s)
- 16 kHz capture -> 1024-pt autocorrelation f0 (65-600 Hz; earliest-peak +
  integer-divisor octave-safe selection, v1.1/v1.3) -> median-3 smoothing ->
  cents = 1200*log2(f0/f_target)
- PASS if |cents| <= tolerance held for 1.0 s within the 4 s window;
  tolerance L1 +/-80, L2 +/-55, L3 +/-35 cents (v1.1 re-tune: 65/45/30 still
  stalled beginners once the detector was fixed); PASS -> +2 XP
- Octave-equivalent grading (v1.2): cents fold into +/-600, so the correct
  pitch class in any octave passes; the SING status shows an OCT-LOW/
  OCT-HIGH hint when folded
- 3 failed attempts -> device plays the target note (show answer) -> next
  note; a sing session = 5 notes, then the shared RESULT page with lifetime
  totals (v1.2)
- Playback and capture both 16 kHz; format switch via bsp_audio_set_format
  serialized on the single audio worker (BSP constraint)

### 3.3 Duet mode (v1.3)

- Round-based: the user sings one 2-4 s phrase; the device transcribes it
to a note sequence, infers the key, then plays the phrase back as a duet:
  melody echo + one harmony voice; the pet hops once per note; +3 XP per
  performed duet (decision: per-duet, no fixed session length)
- Capture: same 16 kHz chain as SING (octave-safe f0 + median-3). Window
  4 s max; ends early after >= 2 confirmed notes + 0.8 s silence
- Segmentation [PURE ear_duet.c]: frame -> nearest semitone; a note is
  confirmed after 3 stable frames (~192 ms, i.e. the >= 190 ms minimum);
  within-note wobble within +/-70 cents stays one note (anti-vibrato);
  an unvoiced gap >= 350 ms closes the note; at most 12 notes kept
- Key inference [PURE]: duration-weighted pitch-class histogram (first
  and last notes add a 50% anchor bonus — melodies start/end on the tonic)
  scored against 24 major/minor templates; weak margin (leader ahead of the
  runner-up by < 10% of sung duration) or < 3 distinct classes -> fallback
  tonic = heaviest class, major (drone/simple phrases). Octave doubling needs no special case:
  pitch classes already fold octaves (an octave-low male voice works)
- Harmony (stage-gated, diatonic in the inferred key):
  Egg/Hatchling = unison echo (quieter twin voice); Chick = diatonic 3rd
  (below, above when below < C3); Fledgling = diatonic 6th (same rule);
  Songbird = per-note 3rd-or-6th chosen for smooth voice leading
  (smallest harmony step). Voices clamp to MIDI 48..81 (C3..A5); a voice
  above A5 folds down one octave (> 880 Hz rule)
- Playback: melody amp 0.9 + harmony amp 0.6 + the harmony doubled one
  octave up at 0.45 on the integer synth (the octave double keeps the
  harmony audible on the small speaker — a plain close-interval harmony
  gets masked into one thicker tone); per-note length = captured duration
  clamped to 200..900 ms plus an 80 ms gap; REVIEW shows the transcription
  + key before playing
- < 2 confirmed notes -> "TOO SHORT" -> OK retries the capture

### 3.4 Dictation mode (v1.4)

- Same idea as DUET, reversed: the device plays a short melody; the user
  sings it back; the sung-back phrase is graded note-by-note against the
  target (see 3.1 for the shared NVS/pet economy)
- Session: 10 melodies at the dictation level (dlvl, starts at 1, kept
  across sessions like the other per-mode levels); 3 notes at LV1, 4 at
  LV2, 5 at LV3; level 1 range C4-G4 with a C-major triad as the final
  note (D4-A5 range and wider leaps at higher levels); no two adjacent
  notes repeat
- Five states: READY (melody shown) -> LISTEN (playback, pet hops once per
  note) -> ECHO (OK starts the capture, UP replays) -> SING (capture, same
  chain as DUET, 4 s cap: once the expected note count is confirmed 0.8 s of
  silence ends it; before that only a 1.6 s pause ends it, so the singer can
  think between notes) -> GRADE (verdict + XP) -> next melody or RESULT
  after 10. Level 1 plays slower (560 ms per note vs 420 / 360) so a 3-note
  melody is easy to hold in memory
- Grading [PURE ear_dictation.c]: the whole phrase is octave-folded, the
  first sung note decides the fold direction (singing an octave up or down
  is accepted; a mid-phrase octave slip still marks the slipped notes
  wrong). Within the fold, pitch tolerance tightens with the level:
  +-90 cents (LV1 EASY), +-70 cents (LV2 NORMAL), +-50 cents (LV3 STRICT)
  - LV1 is wider than the transcriber's own +-70-cent vibrato window, so a
  beginner's wobble never costs a note; LV3 wants the sung pitch within a
  quarter tone. No window reaches a semitone, so singing a different note is
  always wrong however easy the level. Positions are matched in order, and up to
  2 sung notes per position may be skipped as EXTRA, so singing one note
  twice does not shift the rest of the phrase out of alignment; a target
  note with no match is MISS
- Rewards: perfect (all target notes OK, no extras) +2 XP; otherwise all
  target notes OK +1 XP (correct melody); half or more of the target notes
  OK +1 XP (partial credit - singing half the melody right still teaches
  the pet something); below that no XP. Too short (< 2 notes) -> retry the
  capture. Session end applies the same auto level adjust as quiz/sing
- Keys: OK advances the state; UP replays the melody at ECHO; OK long
  quits the session (earned XP stays); the RESULT totals row shows the
  lifetime count of melodies sung back correctly

## 4. Pet Economy

- Stages (cumulative XP): Egg 0 -> Hatchling 10 -> Chick 30 -> Fledgling 80
  -> Songbird 160
- XP sources: quiz correct +1; perfect session (10/10) +3; sing PASS +2/note;
  duet performed +3 (v1.3); dictation perfect +2, fully correct +1, half or
  more of the notes correct +1 (v1.4)
- Reactions: correct -> pet hop + happy blip; wrong -> head shake; evolve ->
  stage image swap + fanfare + hop x3
- XP never decreases (no-punishment principle); no dates, no streaks
- NVS read failure -> treat as new pet, rebuild archive, no crash

## 5. Screens & Button Semantics (redesigned UI, not the demo shell)

HOME
- top: "EAR BUDDY" title + battery % top-right
- center: pet sprite 48x48 + XP bar (180x10) + "LV n cur/next" text
- bottom: mode card cycling INTERVAL / CHORD / SING / DUET / DICTATION /
  SETUP via UP/DOWN (v1.3 adds DUET; v1.4 adds DICTATION)
- UP/DOWN click: switch mode card; OK click: enter mode; OK long: nothing

QUIZ
- header "Q 3/10 LV2"; status "LISTEN..." during playback
- options grid: 2 / 4 / 6 cells (100x44 each), cursor highlight
- footer hints "OK CONFIRM - UPL REPLAY - OKL QUIT" (Montserrat 14)
- UP long: replay question; OK long: quit session (earned XP stays);
  combo >= 3 shown in header
- wrong answer: correct cell highlights 1.5 s with replay, then next

SING
- "SING: E4" big; horizontal cents scale -94..+94 with moving marker;
  hold progress bar; "OK START - UP REPLAY" hint
- OK starts 3 s attempt; live marker; PASS -> "+2 XP" flash -> next note
- 3 fails -> device plays target (show answer) -> next note

DUET (v1.3, five states)
- READY: "DUET" + stage harmony hint (UNISON / 3RD / 6TH / VOICE-LEAD);
  OK starts the capture round
- LISTEN: live note count + running note names; capture auto-ends (see
  3.3); OK long cancels back to READY
- REVIEW: transcription (up to 12 note names) + inferred key ("C maj");
  OK = play the duet, UP = re-sing the phrase
- PLAY: melody + harmony play; the pet hops once per note (frame swap);
  ends -> DONE
- DONE: "+3 XP" + key recap + lifetime duet count; OK -> next round

DICTATION (v1.4, five states)
- READY: "DICTATION" + "MELODY 1/10 LV1 EASY" + the melody as note names;
  OK plays it
- LISTEN: playback (560/420/360 ms per note by level); the pet hops once
  per note; ends -> ECHO
- ECHO: melody stays on screen as the target; "OK SING UP REPLAY OKL EXIT"
- SING: live note count + running note name; transcription auto-ends
- GRADE: "PERFECT! +2 XP" / "RIGHT! +1 XP" / "CLOSE 2/3 +1 XP" /
  "WRONG 1/3" + the sung-back notes (green/red) beside the target; OK ->
  next melody, after 10 -> RESULT

RESULT (after 10 quiz questions)
- "SCORE 8/10" + "+9 XP" + pet hop anim; OK -> HOME

SETUP
- rows: VOLUME (UP/DOWN +/-10, OK save+back); RESET PROGRESS (OK long ->
  full-screen confirm page: a second hold within 5 s resets and returns
  HOME, any key cancels; page added v1.5)
- battery top-right

Global: 10 s idle -> backlight 30%; any key -> 100%. Implemented in ear_ui.

## 6. Button Map (uniform)

| Input | HOME | QUIZ | SING | DUET | DICTATION | RESULT | SETUP |
|---|---|---|---|---|---|---|---|
| UP/DOWN click | switch card | move cursor | UP=replay | UP=re-sing | UP=replay melody | - | select/adjust |
| OK click | enter mode | confirm answer | start attempt | start/confirm/play-next | listen/sing/next | to HOME | save+back |
| OK long | - | quit session | quit | quit | quit session | - | RESET row: open confirm page |
| UP long | - | replay audio | - | - | - | - | - |

OK-long quit never revokes earned XP (XP booked + NVS-written at the moment
of each correct answer / PASS).

## 7. Data & Persistence

NVS namespace earbuddy: xp u32; ilvl/clvl/slvl u8 (1-3); vol u8 (0-100);
answered u32; sing_pass u32; best_combo u8 (lifetime stats, v1.2);
duets u32 key "du" (lifetime duets, v1.3 — old records default to 0);
dlvl u8 (dictation level 1-3) key "dl" + melodies u32 key "mel"
(lifetime melodies with every target note OK - partial credit earns XP but
  does not count here; both v1.4 — old records default to 1 / 0).
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
  ear_duet.c/h      - [PURE] phrase segmentation / key inference /
                      stage-gated harmony line (v1.3)
  ear_dictation.c/h - [PURE] melody gen + octave-folded, per-level
                       tolerance grading (v1.4)
  ear_tone.c/h      - [PURE] additive synth into int16 buffer (C4-A4 table)
  ear_store.c/h     - NVS load/save (namespace earbuddy)
  ear_audio.c/h     - audio worker task: play queue + mic->pitch->event queue
  ear_ui.c/h        - 6 LVGL pages + backlight idle manager
  ear_assets.c/h    - pet sprites 5 stages x 2 frames, 48x48 RGB565
                      descriptors (generated)
tools/gen_pet_assets.py - pixel-art generator (code-drawn egg/chick/bird ->
                      RGB565 C arrays)
tests/test_ear_model.c   - question uniqueness/grading/level boundaries
tests/test_ear_pet.c     - thresholds/XP monotonic/perfect bonus
tests/test_ear_pitch.c   - synthetic sine PCMs -> detected pitch within tol
tests/test_ear_dictation.c - level shapes/no repeats/octave-fold grading,
                       per-level tolerance, no-shift extras, partial credit

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
| Sing tolerance | +/-80/55/35 cents | v1.1: 65/45/30 (itself raised from 50) still stalled beginners; loosened one notch after the octave fix |
| 3 fails -> show answer | yes | avoid stuck states |
| OK-long quit keeps XP | yes | no-punishment principle |
| Idle backlight 30% after 10 s | yes | repo power convention |
| RESET = two OK holds within 5 s, second on a full-screen confirm page | v1.2/v1.5 | one accidental 500 ms hold wiped a pet on device 2026-10-11 |
| Down/harmonic unlock levels | L1 up; L2 +down; L3 +harmonic | teaching order: build the ascending reference first, then direction sensitivity |

## 11. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately.
Flashing + on-device acceptance tomorrow with user consent. Tonight =
code + PRD + host tests + firmware build only.
