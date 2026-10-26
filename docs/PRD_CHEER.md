<p align="right"><a href="PRD_CHEER.zh_CN.md">简体中文</a> | English</p>

# CHEER - Pocket Praise Spirit

> Status: In implementation
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/cheer (from upstream main)
> PRD version: v1.0 (2026-10-22: initial release - praise-card dispenser with
> bag draw, warm chime, brightness-growth spirit, milestone celebrations,
> history browse)
> Mode: user-authorized build; chime synth ported from feature/ear-buddy
> (same repository, device-proven 2026-10-20)

## 1. Product Overview

- UI name: CHEER — a pocket praise spirit you wear
- Fully offline; no network, no account, no microphone use in v1
- Press OK: the spirit hands you one warm praise card (Simplified Chinese)
  with a soft chime; every card you take feeds its glow
- Anti-repeat "praise bag": cards are drawn without replacement until the
  bag empties, then reshuffled - no card repeats until you have seen them
  all, and never the same card twice in a row across a reshuffle
- The spirit grows brighter with collected cards: 6 stages from a faint
  ember to a radiant glow; a stage-up plays a small fanfare celebration

## 2. Hardware Facts

- Same board baseline as the repository guide; battery % top-right with
  the cell voltage underneath (bsp_battery_soc()/bsp_battery_mv(),
  -1 -> "--%" / "--.-V"). Readings are cached in RTC memory so a deep-sleep
  wake immediately shows the pre-sleep values; a 30 s LVGL timer refreshes
  them (and every draw refreshes instantly)
- No RTC: growth is driven by the lifetime collected-count, never by dates
- ES8311 playback only in v1 (no capture); Q15 integer wavetable synth
  ported from EAR BUDDY's ear_tone.c (no-FPU-safe, ~1% CPU)
- Chinese text requires a bundled subset font; see section 5

## 3. Behavior, Screen & Buttons

Single screen, redesigned UI (not the demo shell):

- Top: "CHEER" title; battery % and voltage top-right
- Center: the praise card - category chip (Montserrat 14, DAILY/WORK/
  STUDY/BRAVE) above the card text in the bundled CJK font, up to 3 lines
- Below the card: the spirit, a procedurally drawn wisp whose palette
  brightens across 6 stages (ember -> spark -> glow -> warm -> bright ->
  radiant; thresholds 0/10/30/80/160/320 collected, uncapped beyond)
- Aux line: "PRAISE n" lifetime count + next stage target; at the final
  stage "PRAISE n - MAX GLOW"
- OK click: draw the next card (bag algorithm) + warm 3-note arpeggio;
  count +1; crossing a stage threshold adds a 5-note fanfare and the
  spirit brightens
- UP/DOWN: browse the drawing history (most recent first, cap
  CHEER_HISTORY_CAP; browsing plays no sound and does not change the
  count); the first OK after browsing draws a fresh card
- 10 s idle dims the backlight to 30%; 60 s idle enters deep sleep with
  any-key GPIO wake (~0.6 s to ready; SOC/voltage re-read fresh each boot,
  gauge suspend/restart handshake per the baseline low-power demo)

## 4. Technical Architecture

main/
  cheer.h           - shared constants ([PURE] header)
  cheer_cards.c/.h  - [PURE] praise-card library (84 cards, 4 groups) and
                      the shuffled-bag draw engine (Fisher-Yates over
                      xorshift32, no-repeat across reshuffle) + stage math;
                      zero IDF/LVGL deps
  cheer_tone.c/.h   - [PURE] Q15 wavetable synth ported from ear_tone.c
                      (LUT + 3 partials + attack/decay/release envelope)
  cheer_audio.c/.h  - worker task: serializes bsp_audio calls; plays the
                      3-note draw chime and 5-note stage-up fanfare;
                      volume fixed at 80
  cheer_store.c/.h  - NVS persistence: single u32 lifetime count ("cc")
  cheer_ui.c/.h     - single LVGL screen: card view, history browse,
                      procedural wisp spirit with 6 palettes, idle
                      backlight manager
  cheer_font.c/.h   - generated CJK subset font (24 px, 4 bpp) covering
                      exactly the card library's characters
  main.c            - BSP init -> input dispatch -> draw -> UI/audio
tests/test_cheer_cards.c - library integrity, bag exhaustion/no-repeat,
                      stage thresholds, rng anti-stick
tests/test_cheer_tone.c  - LUT peak, render silence-after-end, no clip

[PURE] modules compile with cc -std=c11 -Wall -Wextra -Werror and are
registered in tools/validate.sh. Bag state lives in RAM only (fresh
shuffle per boot); only the lifetime count persists in NVS.

## 5. Chinese Font Pipeline

- Source: Noto Sans SC (SIL OFL 1.1) - subset = unique characters of
  CHEER_CARDS[] plus the fullwidth punctuation the cards use
- Generation: tools/gen_cheer_font.sh runs lv_font_conv --size 24 --bpp 4
  over the parsed character set; the output is committed as
  main/cheer_font.c and regenerated only when the library changes
- Size estimate: ~420 unique glyphs x ~200 B = 85-130 KB flash; the app
  partition has ~7 MB headroom
- Per docs/development/engineering/lvgl-chinese-fonts.md: the static gate
  compiles a coverage check (tests/test_cheer_font.c) asserting every
  card's characters have glyphs in the generated font

## 6. Risks & AI Default Decisions

| Decision | Value | Rationale |
| --- | --- | --- |
| Card count 84, 4 groups x 21 | variety without bloat | full bag cycle = 84 draws no-repeat |
| 6 stages at 0/10/30/80/160/320 | mirrors EAR BUDDY pacing | proven growth rhythm |
| History cap 20 | RAM bounds | LVGL label reuse |
| Chinese cards, English chrome | audience is Chinese-first | title/hints stay Montserrat |
| No mic / no TTS in v1 | keep v1 shippable today | prosody ideas deferred |
| Risk: fixed cards decay | 84-card bag + stage chase | reviewer-identified weakness |

## 7. Delivery Report Format

Build / Host tests / Device tests / Unverified reported separately. Device
test needs only buttons and ears (no instrument); the battery calibration
run has priority for the device today, so on-device verification may trail
the build gate.
