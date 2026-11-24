#!/usr/bin/env bun
// tools/almanac/gen_tables.mjs — generate the calendar lookup tables and the golden test vectors.
//
// Everything this script writes comes from an INDEPENDENT calendar implementation: the
// lunar-javascript library (https://github.com/6tail/lunar-javascript), the library life.tw cites
// for its almanac pages. Hour-luck vectors come from life.tw almanac pages themselves, because the
// library's hour gods differ from the published almanac on some hours. Nothing is typed by hand.
//
// The library is not a dependency of the firmware or of tools/validate.sh; the outputs are committed.
//
// Usage, from the repository root:
//   bun tools/almanac/gen_tables.mjs --lunar <dir containing node_modules/lunar-javascript> \
//       [--life <life.json>] [--root <repository root>]
//
// --life is optional but strongly recommended: a JSON object keyed by ISO date whose entries carry
// "hours": [{ name: "子時", luck: "吉" | "凶" | "早子凶／晚子吉" }, ...] (12 entries, 子時 first).

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const argv = process.argv.slice(2);
const opt = (name) => {
    const i = argv.indexOf(name);
    return i >= 0 ? argv[i + 1] : undefined;
};

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(opt("--root") ?? join(here, "..", ".."));
const lunarDir = opt("--lunar");
if (!lunarDir) {
    console.error("usage: bun tools/almanac/gen_tables.mjs --lunar <dir> [--life <life.json>]");
    process.exit(2);
}

const imported = await import(pathToFileURL(join(lunarDir, "node_modules", "lunar-javascript", "index.js")).href);
const lib = imported.default ?? imported;
const libVersion = JSON.parse(readFileSync(join(lunarDir, "node_modules", "lunar-javascript", "package.json"), "utf8")).version;

const YEAR_MIN = 1900;
const YEAR_MAX = 2100;
const STEMS = [..."甲乙丙丁戊己庚辛壬癸"];
const BRANCHES = [..."子丑寅卯辰巳午未申酉戌亥"];
const ZHIXING = [..."建除满平定执破危成收开闭"];
const TIANSHEN = ["青龙", "明堂", "天刑", "朱雀", "金匮", "天德", "白虎", "玉堂", "天牢", "玄武", "司命", "勾陈"];
// The twelve 节 in calendar order; each one starts a new month branch (子 after 大雪).
const JIE = ["小寒", "立春", "惊蛰", "清明", "立夏", "芒种", "小暑", "立秋", "白露", "寒露", "立冬", "大雪"];
// Compass order used throughout the firmware: clockwise from north.
const COMPASS = ["正北", "东北", "正东", "东南", "正南", "西南", "正西", "西北"];
const SHA_TO_COMPASS = { 东: 2, 南: 4, 西: 6, 北: 0 };
// The library places the day 贵人 of 庚 days on 正南; two published almanacs place it 东北.
// The firmware follows the published almanacs, so 庚 rows carry -1 and are not compared.
const YANG_GUI_UNTRUSTED_STEM = idxOf(STEMS, "庚");

function idxOf(table, value) {
    const i = table.indexOf(value);
    if (i < 0) throw new Error(`unknown value ${value}`);
    return i;
}

// ---------------------------------------------------------------------------
// Lunar years: spring festival date, leap month, 29/30-day flags.
// ---------------------------------------------------------------------------
const lunarRows = [];
for (let y = YEAR_MIN; y <= YEAR_MAX; y++) {
    const lunarYear = lib.LunarYear.fromYear(y);
    const leap = lunarYear.getLeapMonth();
    const all = lunarYear.getMonths();
    const firstIndex = all.findIndex((mm) => mm.getMonth() === 1);
    // The library lists the neighbouring years' months too: two before, and the next 正月 after a
    // year without a leap month. Keep exactly this lunar year's months.
    const months = all.slice(firstIndex, firstIndex + (leap ? 13 : 12));
    if (months.length !== (leap ? 13 : 12)) throw new Error(`${y}: expected ${leap ? 13 : 12} months, got ${months.length}`);
    const [springYear, springMonth, springDay] = lib.Solar.fromJulianDay(months[0].getFirstJulianDay()).toYmd().split("-").map(Number);
    if (springYear !== y) throw new Error(`${y}: the spring festival falls in ${springYear}`);
    let big = 0;
    let total = 0;
    for (const mm of months) {
        const num = mm.getMonth();
        const isLeap = num < 0;
        const m = Math.abs(num);
        const len = mm.getDayCount();
        if (len !== 29 && len !== 30) throw new Error(`${y}: month ${num} has ${len} days`);
        if (isLeap && m !== leap) throw new Error(`${y}: stray leap month ${num}`);
        if (len === 30) big |= 1 << (isLeap ? 12 : m - 1);
        total += len;
    }
    lunarRows.push({ y, spring: { month: springMonth, day: springDay }, leap, big, firstJdn: months[0].getFirstJulianDay(), total });
}
// Continuity: each lunar year must end exactly where the next one starts.
for (let i = 1; i < lunarRows.length; i++) {
    const prev = lunarRows[i - 1];
    const cur = lunarRows[i];
    if (prev.firstJdn + prev.total !== cur.firstJdn) throw new Error(`${cur.y}: lunar years are not contiguous`);
}

// ---------------------------------------------------------------------------
// The twelve 节 of each civil year, at whole-day resolution (UTC+8).
// ---------------------------------------------------------------------------
const jieRows = [];
for (let y = YEAR_MIN; y <= YEAR_MAX; y++) {
    const row = { month: new Array(12).fill(0), day: new Array(12).fill(0) };
    let solar = lib.Solar.fromYmd(y, 1, 1);
    for (let i = 0; i < 366 && solar.getYear() === y; i++) {
        const k = JIE.indexOf(solar.getLunar().getJieQi());
        if (k >= 0) {
            if (row.month[k]) throw new Error(`${y}: duplicate ${JIE[k]}`);
            row.month[k] = solar.getMonth();
            row.day[k] = solar.getDay();
        }
        solar = solar.next(1);
    }
    for (let k = 0; k < 12; k++) if (!row.month[k]) throw new Error(`${y}: missing ${JIE[k]}`);
    jieRows.push(row);
}

// ---------------------------------------------------------------------------
// Golden day rows from the library.
// ---------------------------------------------------------------------------
const golden = new Map();
const addDay = (y, m, d) => golden.set(`${y}-${m}-${d}`, { y, m, d });
for (const d of [21, 22, 23, 24, 25, 26, 27, 28, 29, 30]) addDay(2026, 11, d);
for (let d = 1; d <= 31; d++) addDay(2026, 12, d);
for (let d = 1; d <= 6; d++) addDay(2027, 1, d);
for (let d = 1; d <= 21; d++) addDay(2027, 2, d);
for (let d = 20; d <= 28; d++) addDay(2025, 7, d); // 闰六月初一 falls on 2025-07-25
for (const [y, m, d] of [[1949, 10, 1], [2000, 1, 1], [2024, 1, 1], [2026, 12, 10], [2100, 12, 31]]) addDay(y, m, d);
// Even spread over the whole range: 149 is coprime to the 60-day pillar cycle.
for (let s = lib.Solar.fromYmd(YEAR_MIN, 1, 31); s.getYear() <= YEAR_MAX; s = s.next(149)) addDay(s.getYear(), s.getMonth(), s.getDay());

const goldenRows = [...golden.values()]
    .sort((a, b) => a.y - b.y || a.m - b.m || a.d - b.d)
    .map(({ y, m, d }) => {
        const l = lib.Solar.fromYmd(y, m, d).getLunar();
        const ygz = l.getYearInGanZhi();
        const mgz = l.getMonthInGanZhi();
        const dgz = l.getDayInGanZhi();
        const dayStem = idxOf(STEMS, dgz[0]);
        return {
            y, m, d,
            lunarYear: l.getYear(), lunarMonth: Math.abs(l.getMonth()), lunarDay: l.getDay(), lunarLeap: l.getMonth() < 0 ? 1 : 0,
            yearStem: idxOf(STEMS, ygz[0]), yearBranch: idxOf(BRANCHES, ygz[1]),
            monthStem: idxOf(STEMS, mgz[0]), monthBranch: idxOf(BRANCHES, mgz[1]),
            dayStem, dayBranch: idxOf(BRANCHES, dgz[1]),
            zhixing: idxOf(ZHIXING, l.getZhiXing()),
            chong: idxOf(BRANCHES, l.getDayChong()),
            sha: SHA_TO_COMPASS[l.getDaySha()],
            tianshen: idxOf(TIANSHEN, l.getDayTianShen()),
            huangdao: l.getDayTianShenType() === "黄道" ? 1 : 0,
            dirCai: idxOf(COMPASS, l.getDayPositionCaiDesc()),
            dirXi: idxOf(COMPASS, l.getDayPositionXiDesc()),
            dirFu: idxOf(COMPASS, l.getDayPositionFuDesc()),
            dirYang: dayStem === YANG_GUI_UNTRUSTED_STEM ? -1 : idxOf(COMPASS, l.getDayPositionYangGuiDesc()),
            label: `${ygz}年 ${mgz}月 ${dgz}日 ${l.getZhiXing()}`,
        };
    });

// ---------------------------------------------------------------------------
// Golden hour rows from saved life.tw pages.
// ---------------------------------------------------------------------------
const hoursRows = [];
const lifePath = opt("--life");
if (lifePath) {
    const life = JSON.parse(readFileSync(lifePath, "utf8"));
    for (const key of Object.keys(life).sort()) {
        const entries = life[key]?.hours;
        if (!Array.isArray(entries) || entries.length !== 12) continue;
        const [y, m, d] = key.split("-").map(Number);
        let mask = 0;
        let count = 0;
        entries.forEach((h, i) => {
            const zi = BRANCHES.indexOf(h.name[0]);
            if (zi !== i) throw new Error(`${key}: hour ${i} is ${h.name}`);
            let luck = h.luck;
            if (luck.includes("／")) luck = luck.split("／")[0]; // 早子凶／晚子吉 → the day's own 子时
            if (luck.endsWith("吉")) mask |= 1 << zi;
            else if (!luck.endsWith("凶")) throw new Error(`${key}: cannot read luck "${h.luck}"`);
            count++;
        });
        hoursRows.push({ y, m, d, mask, count });
    }
}

// ---------------------------------------------------------------------------
// Emit C.
// ---------------------------------------------------------------------------
const pad = (n) => String(n).padStart(2, " ");
const hex4 = (n) => "0x" + n.toString(16).padStart(4, "0");

const tablesC = `// Generated by tools/almanac/gen_tables.mjs from lunar-javascript ${libVersion} — DO NOT EDIT.
#include "almanac_tables.h"

// Lunar years ${YEAR_MIN}..${YEAR_MAX}: spring festival date, leap month (0 = none), 30-day flags.
// big_bits bit i (0..11): month i+1 has 30 days; bit 12: the leap month has 30 days.
const almanac_lunar_year_t almanac_lunar_years[ALMANAC_LUNAR_YEAR_COUNT] = {
${lunarRows
    .map((r) => `    { ${pad(r.spring.month)}, ${pad(r.spring.day)}, ${pad(r.leap)}, ${hex4(r.big)} }, /* ${r.y}: 正月初一 ${r.y}-${String(r.spring.month).padStart(2, "0")}-${String(r.spring.day).padStart(2, "0")} */`)
    .join("\n")}
};

// The twelve 节 of each civil year, in almanac_jie_year_t order, at whole-day resolution.
const almanac_jie_year_t almanac_jie_years[ALMANAC_LUNAR_YEAR_COUNT] = {
${jieRows
    .map((r, i) => `    { { ${r.month.join(", ")} }, { ${r.day.join(", ")} } }, /* ${YEAR_MIN + i} */`)
    .join("\n")}
};
`;

const goldenH = `// Generated by tools/almanac/gen_tables.mjs — DO NOT EDIT.
//
// Day rows come from lunar-javascript ${libVersion}, an implementation independent of
// main/almanac_cal.c. Hour rows come from life.tw almanac pages saved on 2026-11-23 (the library's
// hour gods differ from the published almanac on some hours). Index conventions match
// main/almanac_cal.h: stems 甲..癸, branches 子..亥, compass clockwise from 正北.
#pragma once

#include <stdint.h>

typedef struct {
    int16_t y;
    int8_t m;
    int8_t d;
    int16_t lunar_year;
    int8_t lunar_month; // 1..12
    int8_t lunar_day;   // 1..30
    int8_t lunar_leap;  // 0 or 1
    int8_t year_stem;
    int8_t year_branch;
    int8_t month_stem;
    int8_t month_branch;
    int8_t day_stem;
    int8_t day_branch;
    int8_t zhixing;   // 建除满平定执破危成收开闭
    int8_t chong;     // clashed branch
    int8_t sha;       // compass direction of 煞方
    int8_t tianshen;  // 青龙..勾陈
    int8_t huangdao;  // 1 = 黄道日
    int8_t dir_cai;
    int8_t dir_xi;
    int8_t dir_fu;
    int8_t dir_yang;  // -1 where the reference disagrees with the published almanacs
} almanac_golden_day_t;

static const almanac_golden_day_t k_golden_days[] = {
${goldenRows
    .map((r) => `    { ${r.y}, ${pad(r.m)}, ${pad(r.d)}, ${r.lunarYear}, ${pad(r.lunarMonth)}, ${pad(r.lunarDay)}, ${r.lunarLeap}, ${r.yearStem}, ${r.yearBranch}, ${r.monthStem}, ${r.monthBranch}, ${r.dayStem}, ${r.dayBranch}, ${pad(r.zhixing)}, ${pad(r.chong)}, ${r.sha}, ${pad(r.tianshen)}, ${r.huangdao}, ${r.dirCai}, ${r.dirXi}, ${r.dirFu}, ${r.dirYang} }, /* ${r.label} */`)
    .join("\n")}
};

typedef struct {
    int16_t y;
    int8_t m;
    int8_t d;
    uint16_t luck; // bit i set: 时辰 i (子..亥) is 吉
} almanac_golden_hours_t;

static const almanac_golden_hours_t k_golden_hours[] = {
${hoursRows.map((r) => `    { ${r.y}, ${pad(r.m)}, ${pad(r.d)}, ${hex4(r.mask)} },`).join("\n")}
};
`;

mkdirSync(join(root, "main"), { recursive: true });
mkdirSync(join(root, "tests"), { recursive: true });
writeFileSync(join(root, "main", "almanac_tables.c"), tablesC);
writeFileSync(join(root, "tests", "almanac_golden.h"), goldenH);
console.log(
    `almanac_tables.c: ${lunarRows.length} lunar years, ${jieRows.length} 节 years; ` +
    `almanac_golden.h: ${goldenRows.length} day rows, ${hoursRows.length} hour rows (life.json ${lifePath ? "used" : "NOT given"})`,
);
