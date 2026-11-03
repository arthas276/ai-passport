#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 离线梗条目。数据编译进固件，查找不联网、不写 Flash。
 * gloss 是给 240 宽屏幕准备的一句说明，不是词源论文。 */

#define SLANG_QUERY_MAX 24u

typedef struct {
    const char *title;    /* 列表上的词条 */
    const char *reading;  /* 详情页读法 */
    const char *pinyin;   /* 小写首字母串，供三键检索 */
    const char *short_gloss;
    const char *gloss;
} slang_entry_t;

uint16_t slang_count(void);
const slang_entry_t *slang_at(uint16_t index);

/* 空查询和超长查询都不搜。大小写不敏感，匹配标题、读法、首字母或解释。 */
bool slang_query_ok(const char *query);
uint16_t slang_search(const char *query, uint16_t *out, uint16_t cap);

/* letter 取 a-z 或 A-Z。返回按词条原顺序收集的下标。 */
uint16_t slang_by_initial(char letter, uint16_t *out, uint16_t cap);
