// main/idiom_strings.h - every UI literal in one place. This header is also
// the input of tools/idiom/gen_assets.mjs: the 22 px font subset is derived
// from these strings (plus digits/punctuation), so a new literal here without
// regenerating the font renders as a missing glyph - the generator audits it.
#pragma once

#define STR_APP_NAME     "成语翻牌"
#define STR_HOME_STREAK  "连胜 %u · 最长 %u"
#define STR_HOME_START   "OK 开始"
#define STR_HOME_FOOT    "长按OK 战绩"
#define STR_PLAY_FOOT    "▲▼ 翻牌 · OK 出牌"
#define STR_PLAY_HEAD    "%u/6 · 剩 %u"
#define STR_RESULT_WIN   "猜中了"
#define STR_RESULT_LOSE  "没猜中"
#define STR_RESULT_TRIES "用了 %u/6 · 剩 %u"
#define STR_RESULT_REC   "新纪录"
#define STR_RESULT_FOOT  "OK 再来 · 长按 返回"
#define STR_STATS_TITLE  "战绩"
#define STR_STATS_TOTAL  "总局数"
#define STR_STATS_WINS   "胜场"
#define STR_STATS_RATE   "胜率"
#define STR_STATS_CUR    "当前连胜"
#define STR_STATS_BEST   "最长连胜"
#define STR_STATS_FOOT   "OK 返回 · 长按 说明"
#define STR_HELP_TITLE   "怎么玩"
#define STR_HELP_L1      "绿 位置对 · 黄 字对位错"
#define STR_HELP_L2      "灰 不在词里"
#define STR_HELP_L3      "▲▼ 在剩下的词里翻牌"
#define STR_HELP_L4      "OK 出牌 · 共 6 次"
#define STR_HELP_FOOT    "OK 返回"
#define STR_QUIP_1       "一次就中，牌都还没热"
#define STR_QUIP_2       "两轮锁定，干净利落"
#define STR_QUIP_3       "三轮，节奏刚好"
#define STR_QUIP_4       "四轮拿下，稳住别飘"
#define STR_QUIP_5       "第五轮才翻到，够沉住气"
#define STR_QUIP_6       "压哨命中，屏住呼吸了吧"
#define STR_QUIP_LOSE    "这局词赢了，下局讨回来"

// Big-type words rendered with the 32 px font besides the idiom characters:
#define STR_BIG_WIN      "猜中了"
#define STR_BIG_LOSE     "没猜中"
#define STR_BIG_STATS    "战绩"
#define STR_BIG_HELP     "怎么玩"
