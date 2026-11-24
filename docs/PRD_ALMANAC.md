<p align="right">
  <a href="PRD_ALMANAC.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# ALMANAC v1.0 — IT Almanac: Product Requirements

A pocket almanac for people who run servers, machine rooms and networks. It keeps the traditional
day sheet — lunar date, solar terms, sexagenary pillars, the twelve day officers, the two taboo
lines per day, hour fortune, auspicious directions — and prints an IT-flavoured layer on top: what
is encouraged and what is avoided on a day of rack work, cabling, patching and tickets, a lucky
protocol, port, tool, colour and number, a five-line fortune sheet, and a lucky-date search.

The device is offline and has no battery-backed clock: the app asks for the date once, keeps it
across deep sleep, and asks again after a power loss.

The literal on-screen strings are Chinese; `PRD_ALMANAC.zh_CN.md` is the authority for them and
for the copy deck they come from. This file describes the behaviour in English.

## Scope

- Target: FoloToy AI Passport (ESP32-C3, 8 MB flash, 240×320 ST7789, three keys).
- One screen, one job: a day sheet you walk page by page, plus a menu for the date.
- Everything on screen is deterministic: the same day yields the same sheet on every device.
- No network, no Bluetooth, no audio. The app is a reference you glance at.

## Screens

Every screen shows a centred title (20 px), the battery state at the top right (Montserrat 14),
content in the middle band (y 38–262), and a two-row hint pill at the bottom (y 268–312). Every
screen names both hint rows, so no gesture is hidden.

| Screen | Content | Hint row 1 | Hint row 2 |
| --- | --- | --- | --- |
| Today | date and weekday, sexagenary pillars, lunar date, officer seal, day god, encouraged / avoided lists | change day · details | hold: menu |
| Detail | clash and direction with a gloss, two taboo lines, day god with its line | change day · next page | hold: menu |
| Hours | twelve double-hours in a 4×3 grid with lucky / unlucky marks, plus the two lists | change day · next page | hold: menu |
| Directions | four auspicious directions with an IT reading; lucky protocol, port, tool, colour and number | change day · next page | hold: menu |
| Fortune | five categories, each a star bar and one line | change day · next page | hold: menu |
| Menu | calibrate to today / back to today / lucky date / help / about | choose · confirm | hold: back |
| Lucky date | twelve matters; the search lists up to three dates in the next 30 days | choose matter · confirm | hold: menu |
| Result | the chosen matter and its lucky dates with their officers | re-choose | hold: menu |
| Date setup | year, then month, then day wheel (Montserrat 48 digits) | choose · confirm | hold: cancel |
| Confirm | the chosen date and "count from today" | confirm | hold: cancel |
| Help / About | the how-to text, the app's provenance and disclaimer | back | hold: menu |

The four day pages form a ring: today → detail → hours → directions → fortune → today. The date
is free to move with up and down; a day other than today is tagged as roaming to the right of the date.

## Keys

| Key | Click | Long press |
| --- | --- | --- |
| Up / Down | move the date by a day; move a selection in menu, matter list and date wheel | — |
| OK | next page; confirm a selection; advance the date wheel | open the menu; leave the menu; cancel an edit |

Nothing else is bound.

## Traditional layer

- Lunar calendar with leap months, 1900-01-31 … 2100-12-31, generated from an audited year table.
- Sexagenary pillars: the year pillar changes at the spring festival, the month pillar at each of
  the twelve solar terms with its stem from the spring-festival year, the day pillar from the
  Julian day number.
- The twelve day officers (build, remove, full, balance, settle, hold, break, danger, success,
  receive, open, close) from the day branch against the month branch; the clash (animal), the
  direction and the day god follow the same tables as the hour god, so a lucky hour and a lucky
  day never disagree.
- The two taboo lines per day, one per day stem and one per day branch.
- Four auspicious directions (wealth, joy, fortune, noble helper) by day stem; fortune and noble
  helper in the classical (xie-ji) reading, not the alternate mnemonic reading.

## Picks and rules

- The encouraged and avoided lists each draw three items from a 240-item library across six
  faces: machine room, servers, network, data and security, tickets and people, cloud and
  virtualisation.
- Seeded by the day number, so a day always draws the same items. A mutex table keeps
  contradictory pairs off the same card; a suppressed encouraged item is replaced, not dropped.
- Fortune categories: servers, network, backup, overtime, leaving on time — one to five stars.
- Lucky date search: within 30 days after today, a day qualifies when its officer is lucky for
  the matter, not avoided for it, and no taboo line of that day suppresses it.

## Date and clock

- Times are China Standard Time (UTC+8); the device never sets a time zone.
- On first run, or when the system clock is outside 2020–2100, the app opens the date wheel. It
  offers the last confirmed day when one is stored, otherwise 2026-01-01. The wheel can be
  cancelled back to the today card, which shows the day the wheel was left on. Until a date is
  confirmed, the card shows a reminder in place
  of its date line - the lost-date banner when a day was stored and the clock is gone, the setup
  title when no date was ever set. "Calibrate to today" in the menu then sets the date as well.
- Confirming sets the system clock to that date, keeping the current time of day (noon on a first
  run), and stores the day in NVS.
- The system clock survives deep sleep, so the sheet is still right after a wake press.
- While the device is awake, today rolls over at midnight.

## Power

- Backlight 55 %, dimmed to 8 % after 12 s idle, deep sleep after 60 s.
- Any key press resets the idle timer.
- Deep sleep arms the key GPIO as a low-level wake source and suspends the display, audio, I2C
  and battery gauge in the order the BSP requires; sleep is entered from a worker task, never
  from the LVGL task.

## Rendering and fonts

- Family night-navy base (`#1B263B` background, `#24344D` panels, `#8D99AE` secondary text) with
  the almanac's cinnabar identity colour (`#E0533F`) for the avoided list, the seal and emphasis.
- Two WenKai subsets (16 px and 20 px, 867 glyphs including ASCII) plus a 48 px subset of the
  twelve officer glyphs. Fonts are generated from the string literals of `main/almanac*.{c,h}`;
  the generator fails when a glyph is missing.
- Layout is computed from real glyph advances: `main/almanac_view.c` owns the geometry and every
  line that can appear, and `tests/test_almanac_view.c` measures them over four years of days
  against the box each one is drawn into.

## Tests

| Test | Covers |
| --- | --- |
| `tests/test_almanac_cal.c` | 573 golden calendar days, 38 golden hour rows, pillar and officer rules |
| `tests/test_almanac_pick.c` | draw determinism, mutex table, lucky-date search |
| `tests/test_almanac_clock.c` | time conversion, lost-clock window, setup default |
| `tests/test_almanac_power.c` | dim and sleep thresholds, backlight levels |
| `tests/test_almanac_view.c` | every screen line fits its box, every day of 2026–2029 |

## Not in v1.0

- No custom lucky date (the search is fixed to the twelve matters).
- No solar-term page: the terms only appear inside the month pillar.
- No lunar-date entry: the date wheel takes a solar date.
