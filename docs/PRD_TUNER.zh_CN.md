<p align="right"><a href="PRD_TUNER.md">English</a> | 简体中文</p>

# TUNER 口袋半音调音器 - 产品需求文档

> 状态：可进入固件实现
> 硬件：FoloToy AI Passport（ESP32-C3，240x320 ST7789P3，三 ADC 按键，ES8311）
> 目标分支：feature/tuner（自上游 main 建立）
> PRD 版本：v1.1（2026-10-11：吉他/尤克里里/小提琴弦模式（锁定自动进下一弦）、哼唱模式（音域跟踪）、OK 长按切模式；v1.0 为纯半音核心）
> 日期：2026-10-11
> 模式：用户授权自主开发；音高检测器移植自 feature/ear-buddy 的
> ear_pitch.c（同仓库，2026-09-29 真机验证过）。

## 1. 产品概述

- UI 名称：TUNER，可穿戴半音调音器
- 完全离线；无网络、无账号、无持久化（v1 无设置项）
- 检测 82.4 Hz（吉他低音 E2）到约 1000 Hz 的持续音，显示最近的
  音名+八度、频率、±50 音分指针
- ±5 音分绿区锁定提示音

## 2. 硬件事实（bsp_pins.h / specs / AGENTS）

- 板级基线同仓库指南；电量按默认约定显示在右上角
  （bsp_battery_soc()，-1 显示 --%）；ES8311 以 16kHz 单声道采集+播放
- 上游已启用 Montserrat 14/20；v1 在 sdkconfig.defaults 增加
  Montserrat 48 用于大字号音名（纯英文 UI，无 lv_font_conv）
- 无 RTC；v1 无持久化特性

## 3. 行为、画面与按键

单屏（全新设计，不沿用 demo 外壳），长按 OK 循环切换五个模式：
CHROMATIC 纯半音（默认）、GUITAR（E2 A2 D3 G3 B3 E4）、UKULELE
（G4 C4 E4 A4，reentrant）、VIOLIN（G3 D4 A4 E5）、VOICE 哼唱。

- 顶部："TUNER • <模式>" 标题；右上角电量
- 中央：Montserrat 48 大字音名——弦模式显示当前目标弦，其余显示最近
  音；无声时 "--"。辅助行：弦模式 "STR n/6"，VOICE 模式 "RANGE C3-A4"
  （本次会话所有有声帧的最低/最高音）。下方 Montserrat 20 显示频率
- 指针条宽 200px，围绕参考音（目标弦或最近音）±50 音分，±5 音分绿区，
  游标两端限位；状态词 LOW / HIGH / IN TUNE
- 锁定：连续 8 帧入绿区 -> IN TUNE（绿）+ 一声提示音；弦模式随即
  自动进下一根弦（循环）
- OK 单击冻结/恢复读数（显示 FROZEN）；OK 长按切模式（重置弦序/音域）；
  UP/DOWN 在弦模式中换目标弦；任意按键点亮背光；空闲 10 秒背光 30%
  （仓库功耗约定）

## 4. 技术架构

main/
  tuner.h           - 共享常量/类型（[PURE] 头文件）
  tuner_pitch.c/.h  - [PURE] 自相关检测器，移植自 feature/ear-buddy 的
                      ear_pitch.c：RMS 门限、最早显著峰 + 整数
                      分频复核，防八度误判、抛物线插值、median-3；全音域音名
  tuner_modes.c/.h - [PURE] 模式表（弦调音）、弦步进与锁定推进、
                      哼唱音域跟踪
  tuner_audio.c/.h  - worker：16kHz 连续采集 -> 检测 -> 事件队列；
                      提示音在帧间串行播放
  tuner_ui.c/.h     - 单屏 LVGL UI + 背光空闲管理
  main.c            - BSP 初始化 -> 输入/音频分发任务
tests/test_tuner_pitch.c - 全音域正弦、低音 E2 检测、八度安全、
                      median-3、音名、门限
tests/test_tuner_modes.c  - 模式表、循环顺序、弦步进、音域跟踪

[PURE] 模块零 IDF/LVGL 依赖，cc -std=c11 -Wall -Wextra -Werror 编译
通过并注册进 tools/validate.sh。

## 5. 风险与 AI 默认决策

| 决策 | 值 | 理由 |
| --- | --- | --- |
| 量程 70-1000 Hz | 覆盖 E2..B5 | 吉他/贝斯低音 E 到小提琴 E5 |
| 绿区 ±5 音分 | 紧但可见 | 调音器通行显示 |
| 锁定 = 8 帧（约 0.5 秒） | 稳不拖沓 | 每帧 64ms |
| 不做琴弦目标标记 | v1.1 已加乐器模式 | 吉他/尤克里里/小提琴弦 + 自动进弦 |
| 不做贝斯模式 | v1.1 范围外 | 低音 E1 41Hz 低于 70Hz 检测下限，需更长帧 |
| 无设置/NVS | 不需要 | 音量固定 80；模式开机重置 |
| 风险：低音弦噪声下检测 | 合成测试 + 真机验证 | 最低周期约 229 样本 vs 1024 帧长 |

## 6. 交付报告格式

Build / Host tests / Device tests / Unverified 分报。真机验证需要一个
真实乐器的持续音（由用户提供）。
