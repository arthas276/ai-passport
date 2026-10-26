<p align="right"><a href="PRD_CHEER.md">English</a> | 简体中文</p>

# CHEER - 夸夸精灵

> 状态：实现中
> 硬件：FoloToy AI Passport（ESP32-C3，240x320 ST7789P3，3 个 ADC 按键，ES8311）
> 目标分支：feature/cheer（自 upstream main 拉出）
> PRD 版本：v1.0（2026-10-22：首版——夸夸卡抽取、暖和弦风铃、精灵提亮成长、里程碑庆祝、历史回看）
> 模式：用户授权构建；风铃合成器移植自 feature/ear-buddy 的 ear_tone.c
>（同仓库，2026-10-20 已过真机验证）

## 1. 产品概述

- UI 名称：CHEER——挂在胸前的夸夸精灵
- 完全离线；无网络、无账号、v1 不使用麦克风
- 按 OK：精灵递给你一张暖融融的夸夸卡（简体中文）并配一声轻柔风铃；
  每收下一张卡，它的光就更亮一分
- “夸夸袋”防重复：不放回抽取，抽完一整袋才重新洗牌——一袋之内绝不
  重复，重洗边界上也绝不连出同一张
- 精灵随收集数提亮成长：6 个阶段，从微弱火种到满溢光华；升阶时
  奏响一段小型庆祝和弦

## 2. 硬件事实

- 板级基线同仓库指南；右上角电量百分比，下方一行显示电芯电压（bsp_battery_soc()/bsp_battery_mv()，
  -1 时显示 "--%" / "--.-V"）。读数缓存在 RTC 内存，深睡唤醒立即显示睡前值；
  30 秒 LVGL 定时器自动刷新，抽卡按键时即时刷新
- 无 RTC：成长只由终身收集数驱动，与日期无关
- v1 仅用 ES8311 播放（不采集）；Q15 整数波表合成器移植自
  EAR BUDDY 的 ear_tone.c（无 FPU 安全，约 1% CPU）
- 中文文本需要内置子集字体；见第 5 节

## 3. 行为、屏幕与按键

单一屏幕，全新设计 UI（非 demo 外壳）：

- 顶部："CHEER" 标题；右上角电量百分比与电压
- 中部：夸夸卡——分类角标（Montserrat 14，DAILY/WORK/STUDY/BRAVE）
  在卡片文字上方，卡片文字使用内置中文字体，最多 3 行
- 卡片下方：精灵本体，程序化绘制的小光灵，调色板跨 6 阶段提亮
  （火种→微光→暖光→莹亮→熠熠→满溢；阈值 0/10/30/80/160/320 张，
  之后不设上限）
- 辅行："PRAISE n" 终身计数 + 下一阶段目标；满阶后显示
  "PRAISE n - MAX GLOW"
- OK 单击：抽下一张卡（袋子算法）+ 暖三音琶音；计数 +1；跨过阶段
  阈值时奏 5 音庆祝和弦，精灵提亮
- UP/DOWN：翻看抽取历史（最新在前，上限 CHEER_HISTORY_CAP；翻看
  不发声、不改变计数）；浏览后首次 OK 抽新卡
- 10 秒无操作背光降至 30%；60 秒无操作进入深睡，任意按键经 GPIO 低电平唤醒
  （约 0.6 秒回到就绪；每次开机重读电量/电压，电量计挂起/重启时序同仓库低功耗
  demo 契约）

## 4. 技术架构

main/
  cheer.h           - 共享常量（[PURE] 头文件）
  cheer_cards.c/.h  - [PURE] 夸夸卡文库（84 条、4 组各 21）与洗牌袋
                      引擎（xorshift32 上的 Fisher-Yates，重洗边界
                      不连出）+ 阶段数学；零 IDF/LVGL 依赖
  cheer_tone.c/.h   - [PURE] Q15 波表合成器，移植自 ear_tone.c
                      （LUT + 3 分音 + 起音/衰减/收音包络）
  cheer_audio.c/.h  - worker 任务：串行化 bsp_audio 调用；播放抽卡
                      三音琶音与升阶五音和弦；音量固定 80
  cheer_store.c/.h  - NVS 持久化：单个 u32 终身计数（键 "cc"）
  cheer_ui.c/.h     - 单一 LVGL 屏：卡片视图、历史翻看、程序化光灵
                      （6 套调色板）、背光闲置管理
  cheer_font.c/.h   - 生成的中文子集字体（24 px，4 bpp），覆盖且仅
                      覆盖卡文库全部用字
  main.c            - BSP 初始化 -> 按键分发 -> 抽卡 -> UI/音频
tests/test_cheer_cards.c - 文库完整性、袋子耗尽/不重复、阶段阈值、
                      rng 防卡死
tests/test_cheer_tone.c  - LUT 峰值、结束静音、不削波

[PURE] 模块以 cc -std=c11 -Wall -Wextra -Werror 编译，注册进
tools/validate.sh。袋子状态仅存 RAM（每次开机重新洗牌）；仅终身计数
持久化到 NVS。

## 5. 中文字体管线

- 字源：Noto Sans SC（SIL OFL 1.1）；子集 = CHEER_CARDS[] 全部唯一
  字符 + 卡面使用的全角标点
- 生成：tools/gen_cheer_font.sh 用 lv_font_conv --size 24 --bpp 4 按
  解析出的字符集生成，产物提交为 main/cheer_font.c，仅在文库变化时
  重新生成
- 体积估算：约 420 唯一字 × 约 200 B = 85-130 KB flash；app 分区尚有
  约 7 MB 余量
- 按 docs/development/engineering/lvgl-chinese-fonts.md：静态门禁编译
  覆盖检查（tests/test_cheer_font.c），断言每张卡的每个字符都在生成
  字体内有字形

## 6. 风险与 AI 默认决策

| 决策 | 取值 | 理由 |
| --- | --- | --- |
| 卡量 84 条、4 组各 21 | 多样性不臃肿 | 整袋一轮 = 84 抽不重复 |
| 6 阶段 0/10/30/80/160/320 | 对齐 EAR BUDDY 节奏 | 已验证的成长韵律 |
| 历史上限 20 条 | RAM 边界 | LVGL 标签复用 |
| 卡面中文、界面英文 | 受众以中文为主 | 标题/提示维持 Montserrat |
| v1 无麦克风/无 TTS | 今天可交付 | 韵律相关想法延后 |
| 风险：固定文案会衰减 | 84 卡袋 + 阶段追逐 | 评审指出的弱点 |

## 7. 交付报告格式

Build / Host tests / Device tests / Unverified 分开汇报。真机测试只需
按键和耳朵（不需要乐器）；今日设备以电池校准为优先，真机验证可能
晚于构建门禁。
