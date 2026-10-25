#!/usr/bin/env bash
# gen_cheer_font.sh - thin wrapper: regenerate main/cheer_font.c via the
# cross-platform generator (needs bun; the card library in cheer_cards.c is
# the single source of truth for the glyph subset).
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec "${BUN_BIN:-bun}" "$repo_root/tools/gen_cheer_font.mjs"
