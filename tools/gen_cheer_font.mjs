// gen_cheer_font.mjs - regenerate main/cheer_font.c from main/cheer_cards.c.
// Cross-platform (bun); the card library is the single source of truth for
// the glyph subset. Usage: bun tools/gen_cheer_font.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const cardsPath = resolve(root, "main/cheer_cards.c");
const outPath = resolve(root, "main/cheer_font.c");
const fontArg = process.env.CHEER_FONT_SRC || "C:/Windows/Fonts/NotoSansSC-VF.ttf";
const fontPx = process.env.CHEER_FONT_SIZE || "28";

const src = readFileSync(cardsPath, "utf8");
const texts = [...src.matchAll(/\{ "([^"]+)"/g)].map((m) => m[1]);
if (texts.length === 0) throw new Error("no card texts parsed from cheer_cards.c");
const subset = new Set(texts.join("") + "0123456789!?~·—…、 ");
const chars = [...subset].filter((c) => c.charCodeAt(0) > 0x7f || /[0-9!?~]/.test(c)).sort().join("");
console.log(`gen_cheer_font: ${chars.length} unique glyphs from ${texts.length} cards`);
if (!existsSync(fontArg)) throw new Error(`font not found: ${fontArg} (set CHEER_FONT_SRC)`);

const p = spawnSync("bunx", [
  "--bun", "lv_font_conv",
  "--font", fontArg,
  "--size", fontPx, "--bpp", "4",
  "--format", "lvgl",
  "--lv-include", "lvgl.h",
  "--lv-font-name", "cheer_font",
  "--no-compress", "--no-prefilter",
  "--symbols", chars,
  "--range", "0x20-0x3F",
  "-o", outPath,
], { stdio: ["ignore", "inherit", "inherit"], cwd: root });
if (p.status !== 0) throw new Error(`lv_font_conv failed (exit ${p.status})`);

const missing = new Set([...texts.join("")].filter((c) => !subset.has(c) && c.charCodeAt(0) > 0x7f));
if (missing.size > 0) throw new Error(`coverage miss: ${[...missing].join("")}`);
const generated = readFileSync(outPath, "utf8");
if (!generated.includes("cheer_font")) throw new Error("output lacks cheer_font symbol");
console.log(`gen_cheer_font: wrote ${outPath} (${generated.length} bytes, coverage OK)`);
