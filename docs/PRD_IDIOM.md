<p align="right"><a href="PRD_IDIOM.zh_CN.md">简体中文</a> | English</p>

# IDIOM FLIP - Offline Idiom Wordle

> Status: new application, first implementation 2026-11-05; pending on-device verification
> Hardware: FoloToy AI Passport (ESP32-C3, 240x320 ST7789P3, 3 ADC buttons, ES8311)
> Target branch: feature/idiom (from upstream main)
> PRD version: v1.0 (2026-11-05: initial - 242 curated four-character idioms as both puzzle answers and the "keyboard"; UP/DOWN flips through the candidate list, OK commits a guess as four color-feedback tiles under standard Wordle duplicate rules; the candidate list narrows after every guess (on-device solver), so scrolling only shows words still consistent with all feedback; six guesses per round, win/lose streak and best streak persist in NVS; answer order is a seeded shuffle persisted in NVS so no repeats within a full 242-round cycle; one 32 px hero font for tiles and one 22 px font for chrome, both 4 bpp LXGW WenKai subsets; first boot shows a help overlay once; idle 10 s backlight dim, 60 s deep sleep with button wake)
> Mode: the feedback, candidate-filter, shuffle and stats logic is pure C and host-testable

## Use

The device opens on the home screen: the app logo is the game itself - four tiles in the three feedback states. It never enters the hardware-test menu.

- Home: OK starts a round; a long OK opens the stats page.
- Play: the header shows round progress (e.g. 2/6) and how many candidate words remain. UP/DOWN flips the current candidate (the big four-tile row); a long press accelerates. OK commits the guess: the four tiles color like Wordle - green means the character is in the right position, amber means the character is in the idiom but elsewhere, hollow slate means it is not in the idiom. The three states also differ by fill and glyph lightness, not hue alone. The flip list only contains words still consistent with every committed row, so wrong characters fall out of the list automatically. A long OK forfeits the round.
- Result: shows the answer in green tiles, tries used, streak line, and one restrained quip. OK starts the next round; a long OK returns home.
- Stats: games, wins, win rate, current streak, best streak. OK returns; from stats the help overlay is reachable.
- First boot shows the how-to overlay once.

Everything is offline. No provisioning, no accounts. The backlight dims after 10 idle seconds and the device deep-sleeps after 60; any button wakes it into the app.

## Implementation

The application lives in `main/idiom_core.c` (feedback scoring, candidate filtering, seeded shuffle order, streak statistics - pure C, no LVGL/IDF), `main/idiom_store.c` (one versioned NVS blob), `main/idiom_app.c` (screens and the button state machine), generated `main/idiom_words.c`, and generated fonts `main/idiom_font_32.c` / `main/idiom_font_22.c` (4 bpp LXGW WenKai subsets; `tools/idiom/gen_assets.mjs` regenerates them deterministically from the word list plus the UI string table, and audits glyph coverage). Pins, buttons, display, and deep-sleep shutdown remain owned by the BSP.

Pixel budget (240x320, per-screen, all rows verified against the 320 px height):

- HOME: title 32 px @y30; logo = four 52x52 tiles (2x2, x62/y78) in the three feedback states; streak 22 px @y200; start 22 px @y252; footer 22 px @y292; battery right-top.
- PLAY: header 22 px @y2; board = 6 rows of four 36x32 tiles (x0=42/y0=26, pitch 40/35, ends y233); wheel = four 52x52 tiles @y238 (x0=10, pitch 56); footer 22 px @y296. Total 312 px.
- RESULT: verdict 32 px @y30; answer = four 52x52 green tiles @y84; tries 22 px @y152; streak 22 px @y196; quip 22 px @y228 (wraps to two lines); footer 22 px @y292.
- STATS: title 32 px @y30; five rows 22 px @y84..y228; footer 22 px @y292.
- HELP: title 32 px @y30; three demo tiles 36x32 @y84; four rule lines 22 px @y136..214; footer 22 px @y292.

## Limits

No pinyin gloss in v1 (unverified pronunciation data is worse than none); no sound; no daily-by-date puzzle (the device has no trusted clock - the round order is a persisted shuffle instead); the 242-word list is fixed in v1 (growing it regenerates the word table and fonts via the script).
