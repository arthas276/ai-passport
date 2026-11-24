#!/usr/bin/env bun
// tools/almanac/gen_assets.mjs — build the content tables and the UI font subsets.
//
// Inputs:  tools/almanac/content.txt (the reviewed copy deck) and the string literals of
//          main/almanac*.c / main/almanac*.h.
// Outputs: main/almanac_content.c, main/almanac_font_16.c, main/almanac_font_20.c and
//          main/almanac_font_48.c. All four are committed; never edit them by hand.
//
// Usage, from the repository root:
//   bun tools/almanac/gen_assets.mjs [--font <LXGWWenKai-Regular.ttf>] [--lvfc <lv_font_conv.js>]
//        [--content-only]
//
// The two 16/20 px subsets cover every character of every string literal (after comments are
// stripped), so a glyph can only be missing when a string is built at runtime from characters
// that appear nowhere else. The audit at the end fails the run in that case.

import { spawnSync } from "node:child_process";
import { existsSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, "..", "..");
const MAIN = join(ROOT, "main");

const args = process.argv.slice(2);
const flag = (name) => (args.includes(name) ? args[args.indexOf(name) + 1] : undefined);
const ttf = flag("--font") ?? "D:/claude/otp-publish-v13/fonts/LXGWWenKai-Regular.ttf";
const lvfc = flag("--lvfc") ?? "D:/claude/idiom-staging/node_modules/lv_font_conv/lv_font_conv.js";

const STEMS = [..."甲乙丙丁戊己庚辛壬癸"];
const BRANCHES = [..."子丑寅卯辰巳午未申酉戌亥"];
const ZODIAC = [..."鼠牛虎兔龙蛇马羊猴鸡狗猪"];
const ZHIXING = [..."建除满平定执破危成收开闭"];
const TIANSHEN = ["青龙", "明堂", "天刑", "朱雀", "金匮", "天德", "白虎", "玉堂", "天牢", "玄武", "司命", "勾陈"];
const COMPASS = ["正北", "东北", "正东", "东南", "正南", "西南", "正西", "西北"];
const FACES = ["机房", "服务器", "网络", "数据与安全", "工单与人", "云与虚拟化"];
const WEEKDAYS = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"];
const LUNAR_MONTHS = ["正月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "冬月", "腊月"];
const LUNAR_DAYS = [
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
];

const expect = (condition, message) => {
    if (!condition) throw new Error(message);
};

// ---------------------------------------------------------------------------
// Parse the deck: numbered lists, table rows and inline code spans per section.
// ---------------------------------------------------------------------------
function parseDeck(text) {
    const heads = {};
    const lists = {};
    const tables = {};
    const spans = {};
    let section = null;
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.replace(/\s+$/, "");
        if (/^#{2,3} /.test(line)) {
            const m = /^#{2,3} (\d+(?:\.\d+)?)\.? ?(.*)$/.exec(line);
            section = m ? m[1] : null;
            if (section) heads[section] = m[2];
            continue;
        }
        if (!section) continue;
        const item = /^(\d+)\. (.+)$/.exec(line);
        if (item) {
            (lists[section] ??= []).push(item[2]);
            continue;
        }
        if (line.startsWith("|")) {
            const cells = line.split("|").slice(1, -1).map((c) => c.trim());
            if (cells.every((c) => /^-+$/.test(c))) continue;
            (tables[section] ??= []).push(cells);
            continue;
        }
        for (const span of line.matchAll(/`([^`]+)`/g)) (spans[section] ??= []).push(span[1]);
    }
    return { heads, lists, tables, spans };
}

const deckText = readFileSync(join(HERE, "content.txt"), "utf8");
const deck = parseDeck(deckText);

// The UI chrome strings must be rows of the deck's section 12: the deck is the single source of truth.
const stringsHeader = readFileSync(join(MAIN, "almanac_strings.h"), "utf8");
for (const m of stringsHeader.matchAll(/#define (STR_[A-Z0-9_]+) "((?:[^"\\]|\\.)*)"/g)) {
    const value = JSON.parse(`"${m[2]}"`);
    if (!/[\u4e00-\u9fff]/.test(value)) continue;
    expect(deckText.includes(value), `almanac_strings.h: ${m[1]} is not in the copy deck`);
}

const TABLE_HEADERS = new Set(["用途", "行", "神", "被冲的生肖", "煞方", "时辰", "日型", "吉方", "协议", "端口", "档", "事项", "位置", "屏", "百忌"]);
// Rows of one table inside a section: from its header row up to the next table's header row.
const rows = (section, header) => {
    const table = deck.tables[section];
    expect(Array.isArray(table), `deck section ${section}: no table`);
    const start = table.findIndex((r) => r[0] === header);
    expect(start >= 0, `deck section ${section}: table with header "${header}" not found`);
    const out = [];
    for (const row of table.slice(start + 1)) {
        if (TABLE_HEADERS.has(row[0])) break;
        out.push(row);
    }
    return out;
};
const list = (section, count) => {
    const items = deck.lists[section];
    expect(Array.isArray(items) && items.length === count, `deck section ${section}: expected ${count} numbered items, found ${items?.length}`);
    return items;
};

// ---------------------------------------------------------------------------
// Content.
// ---------------------------------------------------------------------------
const yi = [];
const ji = [];
FACES.forEach((face, f) => {
    const want = `${face}｜宜`;
    const key = `3.${f * 2 + 1}`;
    expect(deck.heads[key] === want, `deck heading ${key} is "${deck.heads[key]}", expected "${want}"`);
    expect(deck.heads[`3.${f * 2 + 2}`] === `${face}｜忌`, `deck heading 3.${f * 2 + 2} is not the 忌 list of ${face}`);
    yi.push(...list(key, 20));
    ji.push(...list(`3.${f * 2 + 2}`, 20));
});

const zhixingRows = rows("2.3", "神");
expect(zhixingRows.length === 12 && zhixingRows.map((r) => r[0]).join("") === ZHIXING.join(""), "deck section 2.3: expected the twelve 建除 gods in order");
const baiji = [...list("4.1", 10), ...list("4.2", 12)];
expect(baiji.slice(0, 10).every((line, i) => line[0] === STEMS[i]), "deck section 4.1: lines must start with the ten stems in order");
expect(baiji.slice(10).every((line, i) => line[0] === BRANCHES[i]), "deck section 4.2: lines must start with the twelve branches in order");

const chongRows = rows("5", "被冲的生肖");
expect(chongRows.length === 12 && chongRows.map((r) => r[0]).join("") === ZODIAC.join(""), "deck section 5: expected the twelve clashed zodiac animals in order");
const shaRows = rows("5", "煞方");
expect(shaRows.length === 4 && shaRows.map((r) => r[0]).join("") === "东南西北", "deck section 5: expected the four 煞方 rows");
const SHA_COMPASS = { 东: 2, 南: 4, 西: 6, 北: 0 };
const shaByCompass = new Array(8).fill(null);
for (const [name, text] of shaRows) shaByCompass[SHA_COMPASS[name]] = text;

const luckyGods = list("6.1", 20);
const unluckyGods = list("6.2", 22);
const hourRows = rows("7.1", "时辰");
expect(hourRows.length === 12 && hourRows.map((r) => r[0]).join("") === BRANCHES.join(""), "deck section 7.1: expected the twelve double-hours in order");
const hourGodRows = rows("7.2", "神");
expect(hourGodRows.length === 12 && hourGodRows.map((r) => r[0]).join("") === TIANSHEN.join(""), "deck section 7.2: expected the twelve gods in order");
const dayTypeRows = rows("7.2", "日型");
expect(dayTypeRows.length === 2 && dayTypeRows[0][0] === "黄道日", "deck section 7.2: expected 黄道日 and 黑道日 rows");

const dirRows = rows("8", "吉方");
expect(dirRows.length === 4, "deck section 8: expected four auspicious directions");
const dirLabels = deck.spans["8"]?.filter((s) => /^[\u4e00-\u9fff]{2} [\u4e00-\u9fff]{2}$/.test(s)) ?? [];
expect(dirLabels.length === 4, "deck section 8: expected four on-screen rows such as 预算 正东");
const dirSay = dirRows.map((r) => r[1]);

const protocols = rows("9.1", "协议");
const ports = rows("9.2", "端口");
const tools = list("9.3", 16);
const colors = list("9.4", 8);
const numbers = list("9.5", 12);
expect(protocols.length === 16 && ports.length === 16, "deck sections 9.1 and 9.2: expected sixteen rows each");
expect(ports.every((r) => /^\d+$/.test(r[0])), "deck section 9.2: port numbers must be digits");
expect(numbers.every((n) => /^\d+$/.test(n)), "deck section 9.5: lucky numbers must be digits");

const luckTable = deck.tables["10"];
expect(Array.isArray(luckTable) && luckTable.length === 6, "deck section 10: expected a header plus five star rows");
const luckNames = luckTable[0].slice(1);
expect(luckNames.length === 5, "deck section 10: expected five fortune columns");
const luckLines = luckTable.slice(1).map((r) => r.slice(1));

const matterRows = rows("11", "事项");
expect(matterRows.length === 12, "deck section 11: expected twelve matters");
const godMask = (text) => {
    let mask = 0;
    for (const name of text.split("、")) {
        const i = ZHIXING.indexOf(name);
        expect(i >= 0, `deck section 11: unknown 建除 god "${name}"`);
        mask |= 1 << i;
    }
    return mask;
};

const exclusionRows = rows("1.1", "百忌");
expect(exclusionRows.length === 22, "deck section 1.1: expected one row per 彭祖百忌 line");
const lineIndex = (first) => (STEMS.includes(first) ? STEMS.indexOf(first) : 10 + BRANCHES.indexOf(first));
const faceIndex = Object.fromEntries(FACES.map((f, i) => [f, i]));
const skipItems = Array.from({ length: 22 }, () => ({ lo: 0n, hi: 0n }));
const hideLucky = new Array(22).fill(0);
const matterSkip = new Array(12).fill(0);
const matterIndex = Object.fromEntries(matterRows.map((r, i) => [r[0], i]));
for (const [line, _action, itemRefs, godRefs, matterRefs] of exclusionRows) {
    const li = lineIndex(line[0]);
    expect(li >= 0 && line.length >= 2 && line[0] + line[1] === line.slice(0, 2), `deck section 1.1: cannot read line "${line}"`);
    for (const ref of itemRefs.split("、")) {
        if (ref === "无") continue;
        const m = /^(.+)-宜-(\d+)$/.exec(ref);
        expect(m && faceIndex[m[1]] !== undefined && Number(m[2]) >= 1 && Number(m[2]) <= 20, `deck section 1.1: bad item reference "${ref}"`);
        const id = faceIndex[m[1]] * 20 + Number(m[2]) - 1;
        if (id < 64) skipItems[li].lo |= 1n << BigInt(id);
        else skipItems[li].hi |= 1n << BigInt(id - 64);
    }
    for (const ref of godRefs.split("、")) {
        if (ref === "无") continue;
        const gi = luckyGods.indexOf(ref);
        expect(gi >= 0, `deck section 1.1: "${ref}" is not a 吉神`);
        hideLucky[li] |= 1 << gi;
    }
    for (const ref of matterRefs.split("、")) {
        if (ref === "无") continue;
        expect(matterIndex[ref] !== undefined, `deck section 1.1: "${ref}" is not a 择吉日 matter`);
        matterSkip[matterIndex[ref]] |= 1 << li;
    }
}

// ---------------------------------------------------------------------------
// Emit main/almanac_content.c.
// ---------------------------------------------------------------------------
const cstr = (s) => JSON.stringify(s);
const carray = (name, values, type = "char *const") => `static const ${type} ${name}[] = {\n${values.map((v) => `    ${cstr(v)},`).join("\n")}\n};\n`;
const u64 = (v) => `0x${v.toString(16).padStart(16, "0")}ull`;

const contentC = `// Generated by tools/almanac/gen_assets.mjs from tools/almanac/content.txt — DO NOT EDIT.
#include "almanac_content.h"

#include <stdint.h>

${carray("k_yi", yi)}
${carray("k_ji", ji)}
${carray("k_zhixing_name", ZHIXING)}
${carray("k_zhixing_gloss", zhixingRows.map((r) => r[1]))}
${carray("k_baiji", baiji)}
${carray("k_chong_object", chongRows.map((r) => r[1]))}
${carray("k_chong_note", chongRows.map((r) => r[2]))}
${carray("k_sha_place", shaByCompass.map((s) => s ?? ""))}
${carray("k_god_lucky", luckyGods)}
${carray("k_god_unlucky", unluckyGods)}
${carray("k_hour_note", hourRows.map((r) => r[2]))}
${carray("k_tianshen_name", TIANSHEN)}
${carray("k_hour_god_gloss", hourGodRows.map((r) => r[2]))}
${carray("k_day_type_line", [dayTypeRows[0][1], dayTypeRows[1][1]])}
${carray("k_dir_label", dirLabels.map((s) => s.slice(0, 2)))}
${carray("k_dir_say", dirSay)}
${carray("k_protocol_name", protocols.map((r) => r[0]))}
${carray("k_protocol_note", protocols.map((r) => r[1]))}
${carray("k_port_number", ports.map((r) => r[0]))}
${carray("k_port_note", ports.map((r) => r[1]))}
${carray("k_tool", tools)}
${carray("k_color", colors)}
${carray("k_number", numbers)}
${carray("k_luck_name", luckNames)}
${carray("k_luck_line", luckLines.flat())}
${carray("k_matter", matterRows.map((r) => r[0]))}
${carray("k_stem", STEMS)}
${carray("k_branch", BRANCHES)}
${carray("k_zodiac", ZODIAC)}
${carray("k_compass", COMPASS)}
${carray("k_weekday", WEEKDAYS)}
${carray("k_lunar_month", LUNAR_MONTHS)}
${carray("k_lunar_day", LUNAR_DAYS)}

// Lucky and avoided 建除 gods, and the 彭祖百忌 lines that suppress a matter, as bit masks.
static const almanac_matter_rule_t k_matter_rule[] = {
${matterRows.map((r, i) => `    { 0x${godMask(r[1]).toString(16).padStart(3, "0")}, 0x${godMask(r[2]).toString(16).padStart(3, "0")}, 0x${matterSkip[i].toString(16).padStart(6, "0")} },`).join("\n")}
};

// 宜 items that a 彭祖百忌 line keeps off the same card, as a 120-bit mask in two halves.
static const almanac_item_mask_t k_skip_items[] = {
${skipItems.map((m) => `    { ${u64(m.lo)}, ${u64(m.hi)} },`).join("\n")}
};

static const uint32_t k_hide_lucky[] = {
${hideLucky.map((m) => `    0x${m.toString(16).padStart(5, "0")},`).join("\n")}
};

const char *almanac_item_text(almanac_side_t side, int item) {
    return side == ALMANAC_YI ? k_yi[item] : k_ji[item];
}

const char *almanac_zhixing_name(int god) { return k_zhixing_name[god]; }
const char *almanac_zhixing_gloss(int god) { return k_zhixing_gloss[god]; }
const char *almanac_baiji_line(int line) { return k_baiji[line]; }
const char *almanac_chong_object(int clashed_zodiac) { return k_chong_object[clashed_zodiac]; }
const char *almanac_chong_note(int clashed_zodiac) { return k_chong_note[clashed_zodiac]; }
const char *almanac_sha_place(int compass) { return k_sha_place[compass]; }
const char *almanac_god_lucky(int n) { return k_god_lucky[n]; }
const char *almanac_god_unlucky(int n) { return k_god_unlucky[n]; }
const char *almanac_hour_note(int hour) { return k_hour_note[hour]; }
const char *almanac_tianshen_name(int god) { return k_tianshen_name[god]; }
const char *almanac_hour_god_gloss(int god) { return k_hour_god_gloss[god]; }
const char *almanac_day_type_line(int huangdao) { return k_day_type_line[huangdao]; }
const char *almanac_dir_label(int kind) { return k_dir_label[kind]; }
const char *almanac_dir_say(int kind) { return k_dir_say[kind]; }
const char *almanac_protocol_name(int n) { return k_protocol_name[n]; }
const char *almanac_protocol_note(int n) { return k_protocol_note[n]; }
const char *almanac_port_number(int n) { return k_port_number[n]; }
const char *almanac_port_note(int n) { return k_port_note[n]; }
const char *almanac_tool(int n) { return k_tool[n]; }
const char *almanac_color(int n) { return k_color[n]; }
const char *almanac_number(int n) { return k_number[n]; }
const char *almanac_luck_name(int category) { return k_luck_name[category]; }
const char *almanac_luck_line(int category, int stars) { return k_luck_line[(category * 5) + (stars - 1)]; }
const char *almanac_matter(int matter) { return k_matter[matter]; }
const char *almanac_stem(int stem) { return k_stem[stem]; }
const char *almanac_branch(int branch) { return k_branch[branch]; }
const char *almanac_zodiac(int zodiac) { return k_zodiac[zodiac]; }
const char *almanac_compass(int compass) { return k_compass[compass]; }
const char *almanac_weekday_name(int weekday) { return k_weekday[weekday]; }
const char *almanac_lunar_month(int month) { return k_lunar_month[month - 1]; }
const char *almanac_lunar_day(int day) { return k_lunar_day[day - 1]; }

void almanac_matter_rule(int matter, uint32_t *lucky, uint32_t *avoid, uint32_t *skip_lines) {
    *lucky = k_matter_rule[matter].lucky;
    *avoid = k_matter_rule[matter].avoid;
    *skip_lines = k_matter_rule[matter].skip_lines;
}

bool almanac_line_skips_item(int line, int item) {
    const almanac_item_mask_t mask = k_skip_items[line];
    return item < 64 ? (mask.lo >> item) & 1u : (mask.hi >> (item - 64)) & 1u;
}

uint32_t almanac_hidden_lucky_gods(int line) { return k_hide_lucky[line]; }
`;

writeFileSync(join(MAIN, "almanac_content.c"), contentC);
if (args.includes("--content-only")) {
    console.log("almanac_content.c written (fonts skipped)");
    process.exit(0);
}

// ---------------------------------------------------------------------------
// Fonts: every character of every string literal in main/almanac*.c and main/almanac*.h.
// ---------------------------------------------------------------------------
const SAMPLES = ["0123456789"];
const sources = readdirSync(MAIN).filter((f) => /^almanac.*\.(c|h)$/.test(f) && !/^almanac_font_\d+\.c$/.test(f));
const literals = [];
for (const file of sources) {
    const text = readFileSync(join(MAIN, file), "utf8").replace(/\/\*[\s\S]*?\*\//g, " ").replace(/^[ \t]*\/\/.*$/gm, " ");
    for (const m of text.matchAll(/"(?:[^"\\\n]|\\.)*"/g)) literals.push(JSON.parse(m[0]));
}
const ascii = Array.from({ length: 95 }, (_, i) => String.fromCharCode(0x20 + i));
const charset = [...new Set([...literals.join(""), ...SAMPLES.join(""), ...ascii.join("")])].filter((c) => c !== "\n").sort().join("");
const godCharset = ZHIXING.join("");

expect(existsSync(ttf), `font file not found: ${ttf}`);
expect(existsSync(lvfc), `lv_font_conv not found: ${lvfc}`);

function buildFont(size, name, symbols) {
    const out = join(MAIN, `${name}.c`);
    const run = spawnSync(process.execPath, [
        lvfc, "--size", String(size), "--bpp", "4", "--no-compress", "--format", "lvgl",
        "--font", ttf, "--symbols", symbols, "--lv-font-name", name, "--lv-include", "lvgl.h", "-o", out,
    ], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
    if (run.status !== 0) {
        console.error(run.stdout ?? "", run.stderr ?? "");
        throw new Error(`lv_font_conv failed for ${name} (exit ${run.status})`);
    }
    const generated = readFileSync(out, "utf8");
    const missing = [...symbols].filter((c) => c !== " " && !generated.includes(c));
    if (missing.length) throw new Error(`${name}: glyphs missing from the font: ${missing.join("")}`);
    expect(generated.includes('#include "lvgl.h"'), `${name}: unexpected include`);
    return generated.length;
}

const sizes = {
    "almanac_font_16": buildFont(16, "almanac_font_16", charset),
    "almanac_font_20": buildFont(20, "almanac_font_20", charset),
    "almanac_font_48": buildFont(48, "almanac_font_48", godCharset),
};

// ---------------------------------------------------------------------------
// tests/almanac_font_metrics.h: the advances the layout test measures with.
// ---------------------------------------------------------------------------
function parseMetrics(name) {
    const text = readFileSync(join(MAIN, `${name}.c`), "utf8");
    const lineHeight = Number(/\.line_height = (\d+)/.exec(text)[1]);
    const lists = {};
    for (const m of text.matchAll(/unicode_list_(\d+)\[\] = \{([^}]*)\}/g)) {
        lists[`unicode_list_${m[1]}`] = m[2].split(",").map((x) => Number(x.trim())).filter((x) => !Number.isNaN(x));
    }
    const dsc = /lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] =\s*\{([\s\S]*?)\n\};/.exec(text)[1];
    const rows = dsc.match(/\{[^}]*\}/g).map((r) => Number(/\.adv_w = (\d+)/.exec(r)[1]));
    const cmaps = /lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{([\s\S]*?)\n\};/.exec(text)[1];
    const adv = new Map();
    for (const block of cmaps.matchAll(/\{[\s\S]*?\}/g)) {
        const body = block[0];
        const num = (key) => Number(new RegExp(`\\.${key} = (\\w+)`).exec(body)[1]);
        const start = num("range_start");
        const rangeLength = num("range_length");
        const listLength = num("list_length");
        const idStart = num("glyph_id_start");
        const list = /\.unicode_list = (\w+)/.exec(body)[1];
        const sparse = list !== "NULL";
        const count = sparse ? listLength : rangeLength;
        for (let i = 0; i < count; i++) {
            const cp = sparse ? start + lists[list][i] : start + i;
            adv.set(cp, rows[idStart + i]);
        }
    }
    return { lineHeight, adv };
}

const size16 = 16;
const size20 = 20;
const metrics16 = parseMetrics("almanac_font_16");
const metrics20 = parseMetrics("almanac_font_20");
const asciiAdv = (metrics) => Array.from({ length: 95 }, (_, i) => metrics.adv.get(0x20 + i) ?? size16 * 16);
const exceptions = (metrics, size) => [...metrics.adv.entries()]
    .filter(([cp, adv]) => cp >= 0x80 && adv !== size * 16)
    .sort((a, b) => a[0] - b[0]);
const exceptions16 = exceptions(metrics16, size16);
const exceptions20 = exceptions(metrics20, size20);

expect(exceptions16.length === exceptions20.length &&
       exceptions16.every(([cp], i) => cp === exceptions20[i][0]),
       "almanac_font_16 and almanac_font_20 disagree on which non-ASCII glyphs are off-width");
expect(exceptions16.every(([cp]) => cp > 0x7F && cp !== 0x3000),
       `a full-width glyph is not exactly its size wide: ${exceptions16.map(([cp]) => cp.toString(16)).join(",")}`);

const arrayOf = (name, values, perLine = 16) => {
    const lines = [];
    for (let i = 0; i < values.length; i += perLine) lines.push(`    ${values.slice(i, i + perLine).join(", ")},`);
    return `static const uint16_t ${name}[] = {\n${lines.join("\n")}\n};\n`;
};

const metricsHeader = `// Generated by tools/almanac/gen_assets.mjs from main/almanac_font_16.c and main/almanac_font_20.c — DO NOT EDIT.
//
// Advance widths in 1/16 px for the host layout test. ASCII 0x20..0x7E and the listed
// non-ASCII exceptions are tabulated; every other glyph is exactly the font size wide, which
// the generator checks before writing this file.
#pragma once

#include <stdint.h>

#define ALMANAC_TEST_FONT16_LINE_H ${metrics16.lineHeight}
#define ALMANAC_TEST_FONT20_LINE_H ${metrics20.lineHeight}

${arrayOf("k_ad_16", asciiAdv(metrics16))}
${arrayOf("k_ad_20", asciiAdv(metrics20))}
static const uint32_t k_ad_exception_cp[] = {
    ${exceptions16.map(([cp]) => `0x${cp.toString(16)}`).join(", ")},
};
${arrayOf("k_ad_exception_16", exceptions16.map(([, adv]) => adv))}
${arrayOf("k_ad_exception_20", exceptions20.map(([, adv]) => adv))}
#define ALMANAC_TEST_ADV_EXCEPTIONS ${exceptions16.length}u
`;
writeFileSync(join(ROOT, "tests", "almanac_font_metrics.h"), metricsHeader);
console.log(`tests/almanac_font_metrics.h written (${exceptions16.length} off-width glyphs)`);
console.log(
    `almanac_content.c written; fonts: ${Object.entries(sizes).map(([k, v]) => `${k} ${v} B`).join(", ")}; ` +
    `charset ${[...charset].length} characters from ${sources.length} sources (${literals.length} literals)`,
);
