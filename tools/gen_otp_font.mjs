import { $ } from "bun";
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const ttf = process.argv[2];
if (!ttf) {
  console.error("usage: bun tools/gen_otp_font.mjs <path-to-LXGWWenKai-Regular.ttf>");
  process.exit(2);
}
const here = dirname(fileURLToPath(import.meta.url));
const symbols = readFileSync(join(here, "otp_font_symbols.txt"), "utf8").replace(/[\r\n]+/g, "");
console.log(`glyphs: ${new Set(symbols).size}`);
await $`bunx lv_font_conv --size 20 --bpp 4 --no-compress --format lvgl --font ${ttf} --symbols ${symbols} -o main/otp_font_20.c`;
const path = "main/otp_font_20.c";
let generated = readFileSync(path, "utf8");
const fixed = generated.replace('#include "lvgl/lvgl.h"', '#include "lvgl.h"');
if (fixed !== generated) writeFileSync(path, fixed);
console.log("wrote main/otp_font_20.c");
