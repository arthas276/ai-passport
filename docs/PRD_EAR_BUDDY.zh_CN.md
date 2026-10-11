<p align="right"><a href="PRD_EAR_BUDDY.md">English</a> | 简体中文</p>

# EAR BUDDY 口袋练耳伙伴 - 产品需求文档

> 状态：可进入固件实现
> 硬件：FoloToy AI Passport（ESP32-C3，240x320 ST7789P3，三 ADC 按键，ES8311）
> 目标分支：feature/ear-buddy（自 main 建立）
> PRD 版本：v1.2（2026-10-11：八度安全选峰、median-3 平滑、唱准容差重调、电量计诊断、八度等价判定、重置二次确认、SING 五音结算、生涯统计）
> 日期：2026-09-28
> 模式：用户预先授权通宵自主开发；AI 自行决定了第 10 节列出的默认值；
> 真机验证安排在次日晚间用户在场时进行。

## 1. 产品概述

- UI 名称：EAR BUDDY，口袋练耳工具 + 轻量宠物养成
- 内部前缀：ear_
- 完全离线；无网络、无账号；进度存 NVS
- 用户已用 xiaozhi.me 固件验证同一硬件的 Wi-Fi+麦克风+扬声器链路
- 背景：用户最初考虑 NFC 门卡模拟 - 不可行（NTAG213 是被动标签，无 MCU
  接口）；改选音频方向；用户选定三个模式：音程+和弦听辨、唱音准、宠物养成

## 2. 硬件事实（bsp_pins.h / specs / AGENTS）

- ESP32-C3，8MB Flash，无 PSRAM，LV_MEM 24KB - UI 轻量，无全屏动画
- 无 RTC 时间源 - MVP 不做日期/连续打卡功能（与番茄钟 PRD 同样取舍）
- sdkconfig 只启用 Montserrat 14/20 - MVP UI 用英文；中文 UI 需要
  lv_font_conv（node.js），推迟并列在 Unverified
- ES8311：采样率切换走 bsp_audio_set_format（内部 close/open）；PCM 阻塞；
  所有音频操作串行在单一 worker task
- 电量按仓库默认约定显示在右上角（bsp_battery_soc()，-1 显示 --%）；
  SETUP 页加显 SOC% + 电芯电压（v1.1，CW2017 profile 未按实芯标定）
- 空闲 10 秒背光调至 30%，任意按键恢复 100%（仓库功耗约定）

## 3. 模式与规则

### 3.1 听辨（音程/和弦）

- 一局 10 题；根音 C4-A4；一律上行（下行/和声留后续版本）
- 选项集随难度：
  - 音程：L1 {m3, M3}；L2 {m3, M3, P4, P5}；L3 {m2, M2, m3, M3, P4, P5}
  - 和弦：L1 {maj, min}；L2 {maj, min, dim, aug}；L3 {maj, min, dim, aug, M7, 7}
- 答题：UP/DOWN 移光标，OK 确认；答对 +1 XP，宠物跳 + 开心音效；答错高亮
  正确项并回放正确答案，进入下一题；不扣分
- 每模式自动难度：局末 score>=8 升一级（上限 3）；<=4 降一级（下限 1），NVS 保存
- 连击仅作展示
- 音色：加性合成（f + 0.40*2f + 0.18*3f，v1.3 调浓），快攻击 + 缓衰减至
  ~64% + 8ms 释放坡，16000Hz 16bit（板上验证过的采样率；22050 实测失真）
  单声道；音程 = 两音各 650ms 间隔 120ms；和弦 = 三音齐奏 1250ms；唱音准
  目标音预听 950ms（v1.3：按用户要求更长更饱满）

### 3.2 唱音准

- 播放目标音（C4-A4），屏显音名，UP 重听，OK 开始收音（最长 4 秒）
- 16kHz 采集 -> 1024 点自相关 f0（65-600Hz；v1.1 最早显著峰 + v1.3 整数
  分频复核，防八度误判）-> median-3 平滑 -> 音分偏差 = 1200*log2(f0/f_target)
- 保持 |cents| <= 容差达 1.0 秒判 PASS；容差 L1 ±80、L2 ±55、L3 ±35 音分
  （v1.1 重调：检测修复后旧值仍卡初学者）；PASS +2 XP
- 八度等价判定（v1.2）：音分折叠到 ±600，唱对音高类即算通过；屏显
  OCT-LOW/OCT-HIGH 提示
- 同一目标音失败 3 次后设备播放正确音（show answer）进入下一音；一组
  5 音，结束进共用 RESULT 结算页并显示生涯统计（v1.2）

### 3.3 宠物经济

- 阶段阈值（累计 XP）：蛋 0 -> 破壳 10 -> 雏鸟 30 -> 幼鸟 80 -> 鸣禽 160
- XP 来源：听辨答对 +1；满勤（10/10）+3；唱准 +2/次
- 反应：答对宠物跳 + 音效；答错摇头；进化 = 阶段图切换 + 号角 + 跳 x3
- XP 永不减少；NVS 读取失败按新宠物处理，不崩溃

## 4. 画面与按键（重新设计，不沿用 demo 外壳）

HOME：标题 + 右上角电量；宠物图 48x48 居中；XP 条 + 文本；底部模式卡
（INTERVAL/CHORD/SING/SETUP，UP/DOWN 切换，OK 进入）。
QUIZ：头部 "Q 3/10 LV2"；状态行 LISTEN.../判定反馈；选项网格 2/4/6 格；
底部按键提示。UP 长按重播题目；OK 长按退局（已得 XP 保留）。
SING：目标音名大字；横向音分标尺 + 移动标记 + 保持进度条；UP 重听；OK 开始
收音；OK 长按退出。
RESULT：SCORE n/10 + 本局 XP + 宠物跳动画；OK 回主页（有进化则庆祝）。
SETUP：UP/DOWN 调音量（±10，立即生效），OK 保存返回；OK 长按 = 重置进度
（二次确认页推迟到后版）。
全局：任意按键与音频事件都刷新空闲计时；10s 空闲背光 30%。

## 5. 数据与持久化

NVS namespace earbuddy：xp u32；ilvl/clvl/slvl u8（1-3）；vol u8（0-100）；
answered u32；sing_pass u32；best_combo u8（生涯统计，v1.2）。
读取失败按默认（新宠物）；XP 变化即写（每局 1-2 次，NVS 磨损无忧）；写失败仅日志。

## 6. 技术架构

main/
  main.c            - app_main：BSP 初始化 -> ear boot -> 输入分发任务
  ear_game.h        - 共享类型/常量（阶段/XP/种类/级别表/合成 API）
  ear_model.c       - [PURE] 出题/判分/难度调整/局统计
  ear_pet.c         - [PURE] XP->阶段映射/进度辅助
  ear_pitch.c       - [PURE] 整数域自相关 f0 + 插值 + 置信度 + cents
  ear_tone.c        - [PURE] 加性合成（C4-A4，16000Hz）
  ear_store.c/h     - NVS 持久化（namespace earbuddy）
  ear_audio.c/h     - 单一音频 worker：播放队列 + mic->pitch->事件
  ear_ui.c          - 五页 UI + 背光空闲管理（次日晚间验证）
  ear_assets.c/h    - 宠物 5 阶段 x2 帧 48x48 RGB565（脚本生成）
tools/gen_pet_assets.py - 像素画生成器
tests/test_ear_model.c / test_ear_pet.c / test_ear_pitch.c / test_ear_tone.c

[PURE] 模块零 IDF/LVGL 依赖，gcc -std=c11 -Wall -Wextra -Werror 编译通过并
已注册进 tools/validate.sh（funbox 先例：feature 分支可改 validate.sh）。

## 7. 风险与未验证项

| 风险 | 缓解 |
|---|---|
| 扬声器/麦克风未实测 | xiaozhi 证明链路可用；明晚真机验收听感/拾音 |
| 中文 UI 推迟 | 需 lv_font_conv/node 环境；MVP 英文 UI |
| 合成音色需真机调参 | 参数集中在 ear_tone.c |
| 无 RTC - 无日期功能 | 与番茄钟 PRD 同样裁剪 |
| LVGL 9.5 API 以 dependencies.lock 为准 | 对照 managed_components 头文件 |

## 8. AI 默认决策清单（供用户确认）

| 决策 | 值 | 理由 |
|---|---|---|
| UI 语言 | 英文 | 今晚无 lv_font_conv/node 环境 |
| 音色 | 合成 | 采样需授权清洗；参数集中可调 |
| 音域 | C4-A4 | 小扬声器友好 + 标准练耳音区 |
| 答错 | 不扣分 | 无惩罚养成 |
| 阶段阈值 | 10/30/80/160 XP | 约 2-3 周日常练习到满级 |
| 唱准容差 | ±80/55/35 音分 | v1.1：65/45/30（已从 50 放宽过一轮）仍卡初学者；八度修复后再放宽一档 |
| 3 失败显示答案 | 是 | 避免卡关 |
| OK 长按退局保留 XP | 是 | 无惩罚原则 |
| 空闲 10s 背光 30% | 是 | 仓库功耗约定 |
| RESET = 5 秒内两次 OK 长按 | v1.2 | 真机事故：一次误长按清空了宠物 |
| 电量右上角 | 是 | 仓库默认约定 |

## 9. 交付报告格式

Build / Host tests / Device tests / Unverified 分报。烧录与真机验收在次日晚
进行（需用户授权烧录）。今晚交付 = 代码 + PRD + host tests + 固件构建。
