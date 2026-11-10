// cheer_cards.c - praise-card library + shuffled-bag engine. PURE C.
//
// The card texts below are the single source of truth for the CJK font
// subset: tools/gen_cheer_font.mjs collects every unique character of
// CHEER_CARDS[] and generates main/cheer_font.c from exactly that set.
// Voice: specific, deadpan-warm, lightly teasing - no hollow pep talk.
#include <stdbool.h>

#include "cheer_cards.h"

const cheer_card_t CHEER_CARDS[] = {
    // ---- DAILY ----
    { "按时吃了三顿饭，这事你干得不错", CHEER_GRP_DAILY },
    { "今天没对任\n何人发\n火，记一笔", CHEER_GRP_DAILY },
    { "把外卖吃出堂食的认真，是种能力", CHEER_GRP_DAILY },
    { "收拾桌子的样子，像在给明天递台阶", CHEER_GRP_DAILY },
    { "洗完澡才躺\n下的夜晚，\n是加分项", CHEER_GRP_DAILY },
    { "垃圾是今天\n倒的吧，\n我看见了", CHEER_GRP_DAILY },
    { "你走路带\n风，风都\n让了你三次", CHEER_GRP_DAILY },
    { "今天的你，电量比手机耐用", CHEER_GRP_DAILY },
    { "少喝的那杯奶茶，身体悄悄记下了", CHEER_GRP_DAILY },
    { "哼歌跑调，但心情是真的", CHEER_GRP_DAILY },
    { "早起了十分钟，全天都赚了", CHEER_GRP_DAILY },
    { "今天的黑眼圈，是昨晚认真的勋章", CHEER_GRP_DAILY },
    { "你挑的水果，就没有不甜的", CHEER_GRP_DAILY },
    { "下雨天带了伞，这叫远见", CHEER_GRP_DAILY },
    { "今天笑的次\n数，超出\n平均值了", CHEER_GRP_DAILY },
    { "阳台那盆花，还好有你", CHEER_GRP_DAILY },
    { "杯子里的半\n杯水，明天\n也能喝完", CHEER_GRP_DAILY },
    { "袜子配了对，秩序感回来了", CHEER_GRP_DAILY },
    { "今天没熬夜，身体记住了", CHEER_GRP_DAILY },
    { "便利店关东\n煮，因你\n多卖了串", CHEER_GRP_DAILY },
    { "坐姿端正了\n三分钟，\n腰很感动", CHEER_GRP_DAILY },
    { "泡面里卧了\n个蛋，档次\n不一样了", CHEER_GRP_DAILY },
    { "钥匙一摸就在，秩序感满分", CHEER_GRP_DAILY },
    { "睡前手机放远了，这波高级", CHEER_GRP_DAILY },
    { "今天的午\n睡，睡出了\n周末的质量", CHEER_GRP_DAILY },
    { "水今天喝够\n了，身体\n悄悄记账", CHEER_GRP_DAILY },
    { "那个黄灯没闯，命是自己的", CHEER_GRP_DAILY },
    { "洗的衣服全\n干了，天\n气也帮你", CHEER_GRP_DAILY },
    { "快递纸箱压平了再扔，讲究", CHEER_GRP_DAILY },
    { "电梯让人先下，风度还在", CHEER_GRP_DAILY },
    { "挑的西红柿够红，眼光在线", CHEER_GRP_DAILY },
    // ---- WORK ----
    { "会上没人接的话，你接了", CHEER_GRP_WORK },
    { "周报写得不\n虚，这是\n稀缺品质", CHEER_GRP_WORK },
    { "改到第三版\n还没骂人，\n修养在线", CHEER_GRP_WORK },
    { "那个没人想碰的坑，你填了", CHEER_GRP_WORK },
    { "摸鱼归来还\n能满血，\n这叫弹性", CHEER_GRP_WORK },
    { "回消息像在救火，这叫可靠", CHEER_GRP_WORK },
    { "今天干的活，对得起工资条", CHEER_GRP_WORK },
    { "老板没看见\n的活，我替\n他看见了", CHEER_GRP_WORK },
    { "把烂需求聊成了人话，专业", CHEER_GRP_WORK },
    { "下班的瞬间，你比谁都体面", CHEER_GRP_WORK },
    { "工位再乱，\n你一秒就\n能找到笔", CHEER_GRP_WORK },
    { "你提的那句\n风险，后\n来应验了", CHEER_GRP_WORK },
    { "会议缩短十\n分钟，有\n你的功劳", CHEER_GRP_WORK },
    { "你没甩锅，锅自己碎了", CHEER_GRP_WORK },
    { "打断你八次\n的人，都被\n你忍住了", CHEER_GRP_WORK },
    { "今天没说\n「随便」，\n决策力在线", CHEER_GRP_WORK },
    { "你删掉的那\n段代码，\n没人敢删", CHEER_GRP_WORK },
    { "需求变了三次，你只翻了一次白眼", CHEER_GRP_WORK },
    { "准点出现的\n你，比打\n卡机可靠", CHEER_GRP_WORK },
    { "甲方今天没\n发作，是\n你的功劳", CHEER_GRP_WORK },
    { "把截止日期活成了截止日期", CHEER_GRP_WORK },
    { "把甩来的锅，稳稳放了回去", CHEER_GRP_WORK },
    { "拒绝了无效\n会议，时\n间回来了", CHEER_GRP_WORK },
    { "新人问的问题，你答得耐心", CHEER_GRP_WORK },
    { "文档补全了，后来人会谢你", CHEER_GRP_WORK },
    { "截止日前交了，还提前半天", CHEER_GRP_WORK },
    { "电话里总结\n的三条，\n全被采纳", CHEER_GRP_WORK },
    { "报表一遍就对，眼睛值钱", CHEER_GRP_WORK },
    { "帮实习生救\n的那段代\n码，算积德", CHEER_GRP_WORK },
    { "跨部门那场架，你劝得体面", CHEER_GRP_WORK },
    { "今天没硬撑，也没摆烂，节奏刚好", CHEER_GRP_WORK },
    // ---- STUDY ----
    { "错题本又厚\n了一页，\n是真在学", CHEER_GRP_STUDY },
    { "坐得住冷板\n凳的人，现\n在不多了", CHEER_GRP_STUDY },
    { "那个概念，\n你今天真\n的搞懂了", CHEER_GRP_STUDY },
    { "背了五十个词，忘十个，净赚四十", CHEER_GRP_STUDY },
    { "你问的问题，老师记住了", CHEER_GRP_STUDY },
    { "慢慢啃，骨头缝里都是营养", CHEER_GRP_STUDY },
    { "今天的笔\n记，未来的\n你会回来抄", CHEER_GRP_STUDY },
    { "卡住的地\n方，恰好\n在分水岭上", CHEER_GRP_STUDY },
    { "图书馆的\n灯，为你多\n亮了一会儿", CHEER_GRP_STUDY },
    { "复习没逃，及格线提前谢你", CHEER_GRP_STUDY },
    { "你划的重点，后来都考了", CHEER_GRP_STUDY },
    { "一页一页来，书又不会跑", CHEER_GRP_STUDY },
    { "关掉短视频\n去背书，这\n操作很顶", CHEER_GRP_STUDY },
    { "今天的专注，够写进简历", CHEER_GRP_STUDY },
    { "同桌刷手机\n的时候，\n你在变强", CHEER_GRP_STUDY },
    { "错一次就会，这是天赋的另一种形态", CHEER_GRP_STUDY },
    { "倍速听懂，\n脑子转得\n比老师快", CHEER_GRP_STUDY },
    { "背单词软件今天先认输了", CHEER_GRP_STUDY },
    { "把「明天再说」过成了「今天说了」", CHEER_GRP_STUDY },
    { "草稿纸的用\n量，就是思\n考的密度", CHEER_GRP_STUDY },
    { "交卷前检查了名字，稳", CHEER_GRP_STUDY },
    { "第三遍才懂\n的知识\n点，也作数", CHEER_GRP_STUDY },
    { "笔记记得\n少，但句\n句是自己的", CHEER_GRP_STUDY },
    { "睡前那一页书，真看进去了", CHEER_GRP_STUDY },
    { "错题全标记\n了，下次它\n们死定了", CHEER_GRP_STUDY },
    { "网课没开倍速，一遍过", CHEER_GRP_STUDY },
    { "打卡一百\n天，手腕\n都认识你了", CHEER_GRP_STUDY },
    { "给同学讲懂了，这才算真懂", CHEER_GRP_STUDY },
    { "那个问题，你追到了根上", CHEER_GRP_STUDY },
    { "草稿纸正面\n写满，翻\n面接着来", CHEER_GRP_STUDY },
    { "考前没慌，按计划过了一遍", CHEER_GRP_STUDY },
    // ---- BRAVE ----
    { "手心出汗还\n是举了\n手，这叫勇", CHEER_GRP_BRAVE },
    { "没等「准备\n好」，直\n接开始了", CHEER_GRP_BRAVE },
    { "拒绝得干脆，对方反而敬你", CHEER_GRP_BRAVE },
    { "道歉说出口，比赢一场难", CHEER_GRP_BRAVE },
    { "第一次总是笨拙的，你迈了", CHEER_GRP_BRAVE },
    { "把「算了」\n咽回去，又\n试了一次", CHEER_GRP_BRAVE },
    { "紧张着上\n台，比淡定\n上台了不起", CHEER_GRP_BRAVE },
    { "敢说「这个我不懂」，进步开始计时", CHEER_GRP_BRAVE },
    { "一个人吃\n饭，一个人\n扛事，都算", CHEER_GRP_BRAVE },
    { "退出不合适\n的局，是成\n年人的勇", CHEER_GRP_BRAVE },
    { "没跟风，守住了自己的判断", CHEER_GRP_BRAVE },
    { "深夜那条消息，你发对了", CHEER_GRP_BRAVE },
    { "认输认得体面，也是赢", CHEER_GRP_BRAVE },
    { "说出真实想\n法，空气\n都干净了", CHEER_GRP_BRAVE },
    { "摔了，骂了一句，站起来了", CHEER_GRP_BRAVE },
    { "那通电话拖\n了三天，\n还是打了", CHEER_GRP_BRAVE },
    { "你替别人说了句公道话", CHEER_GRP_BRAVE },
    { "眼泪擦干的\n速度，比\n想象中快", CHEER_GRP_BRAVE },
    { "敢麻烦别人，是信任的开始", CHEER_GRP_BRAVE },
    { "今天没有逃，日子认了", CHEER_GRP_BRAVE },
    { "重新开始的人，手里都握着旧地图", CHEER_GRP_BRAVE },
    { "群里那句大实话，是你说的", CHEER_GRP_BRAVE },
    { "体检报告点开看了，勇", CHEER_GRP_BRAVE },
    { "删掉消耗你的人，手起刀落", CHEER_GRP_BRAVE },
    { "第一次进自由重量区，没缩", CHEER_GRP_BRAVE },
    { "请路人帮拍合影，你开的口", CHEER_GRP_BRAVE },
    { "拒绝那个电话，没编理由", CHEER_GRP_BRAVE },
    { "承认搞砸了，然后修好了", CHEER_GRP_BRAVE },
    { "提加薪的三\n个理由，\n写得漂亮", CHEER_GRP_BRAVE },
    { "独自看完恐怖片，没快进", CHEER_GRP_BRAVE },
    { "对医生的疑\n问，问清\n楚了才走", CHEER_GRP_BRAVE },
};

const size_t CHEER_CARD_COUNT = sizeof(CHEER_CARDS) / sizeof(CHEER_CARDS[0]);

uint32_t cheer_rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x9E3779B9u;   // never stick at zero
    return *state;
}

void cheer_bag_init(cheer_bag_t *bag, uint32_t seed, uint8_t count) {
    bag->rng = seed ? seed : 1u;
    bag->count = count;
    bag->pos = 0;
    bag->last = -1;
    for (uint8_t i = 0; i < count; i++) bag->order[i] = i;
    // Fisher-Yates: shuffle the whole deck once.
    for (uint8_t i = count - 1; i > 0; i--) {
        uint32_t r = cheer_rng_next(&bag->rng) % (uint32_t)(i + 1);
        uint8_t t = bag->order[i];
        bag->order[i] = bag->order[r];
        bag->order[r] = t;
    }
    // An all-identity shuffle is statistically possible for tiny decks;
    // with 124 cards it cannot happen, but guard anyway: swap the first two
    // if the bag came out fully sorted (deterministic, test-friendly).
    bool sorted = true;
    for (uint8_t i = 0; i < count; i++) {
        if (bag->order[i] != i) { sorted = false; break; }
    }
    if (sorted && count > 1) {
        bag->order[0] = 1;
        bag->order[1] = 0;
    }
}

int cheer_bag_draw(cheer_bag_t *bag) {
    if (bag->count == 0) return -1;
    if (bag->pos >= bag->count) {
        int16_t prev_last = bag->last;
        cheer_bag_t fresh;
        cheer_bag_init(&fresh, cheer_rng_next(&bag->rng), bag->count);
        // keep the new rng state and deck, but preserve the no-repeat rule
        bag->rng = fresh.rng;
        for (uint8_t i = 0; i < bag->count; i++) bag->order[i] = fresh.order[i];
        bag->pos = 0;
        if (bag->order[0] == prev_last && bag->count > 1) {
            bag->order[0] = bag->order[1];
            bag->order[1] = (uint8_t)prev_last;
        }
    }
    int idx = bag->order[bag->pos++];
    bag->last = (int16_t)idx;
    return idx;
}

int cheer_stage_of(uint32_t count) {
    static const uint32_t th[CHEER_STAGE_COUNT] = { 0, 10, 30, 80, 160, 320 };
    int s = 0;
    for (int i = 0; i < CHEER_STAGE_COUNT; i++) {
        if (count >= th[i]) s = i;
    }
    return s;
}

uint32_t cheer_stage_floor(int stage) {
    static const uint32_t th[CHEER_STAGE_COUNT] = { 0, 10, 30, 80, 160, 320 };
    if (stage <= 0) return 0;
    if (stage >= CHEER_STAGE_COUNT) stage = CHEER_STAGE_COUNT - 1;
    return th[stage];
}
