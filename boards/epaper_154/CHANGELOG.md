# [0.7.10] - 2026-08-09

Add an interactive all-test app for ePaper-1.54.

### Changes

- `main.cpp` now enters the ePaper-only app when `BSP_EPAPER_INTERACTIVE_TEST_APP=1`.
- Each test is shown as its own EPD page and reruns on entry.
- Touch navigation uses left half = previous page, right half = next page, so any page can be repeated.
- The final summary page prints the latest PASS/WARN/FAIL state for every page to serial log.

### Validation

- `pio run -e epaper_154` passed.
- `pio run -e lcd_085 -e touch_lcd_154 -e amoled_206 -e geek` passed.

# [0.7.9] - 2026-08-09

用户已确认听见 1kHz 与录音回放，音频链路完成闭环验证。

### 变更

- `BSP_HAS_AUDIO_OUT` / `BSP_HAS_AUDIO_IN` 置 1。
- `Audio` 自检从 `WARN ... ear?` 升级为 `PASS ... heard`。
- 音频状态升级为 `[OK]`：ES8311 身份、I2S TX/RX、PA/喇叭、板载麦克风录入与回放均已覆盖。

### 实测结果

- v0.7.8 机器证据：`got48000 peak19340 avg748`，`played48000 id8311`。
- 用户人耳确认：听见测试音与录音回放。

# [0.7.8] - 2026-08-09

新增 ES8311 音频专项测试，覆盖放音、录音和录音回放。

### 变更

- 新增 `audio.h/.cpp`：ES8311 全双工 I2S 驱动，输出走 `DSDIN`，输入走 `ASDOUT`，时钟走 `MCLK/SCLK/LRCK`。
- `Audio` 自检页会先播放 1kHz，再提示说话录音 3 秒，最后把录到的样本原样回放。
- 录音侧打印 `got/peak/avg`，用机器指标判断 I2S 输入是否真有数据。
- `BSP_HAS_AUDIO_OUT` / `BSP_HAS_AUDIO_IN` 暂不打开：本轮机器证据成立，但放音和回放内容还需要耳朵确认。

### 实测结果

- ES8311 初始化成功：`ES8311 ready: 16000 Hz vol75 id8311`。
- 录音：`got48000 peak19340 avg748`，3 秒 16kHz 样本完整读满，且峰值/均值明显非零。
- 回放：`played48000 id8311`，录音样本完整送入 I2S TX。
- 自检：`PASS Mic got48000 peak19340 avg748 replay`；`WARN Audio id8311 tone+replay ear?`。
- 全局自检：`14 PASS / 3 WARN / 0 FAIL`。额外 WARN 是本轮 `TouchP no-press-seen`；`Touch4 maskF` 已覆盖触摸坐标路径。

### 边界

- `Mic PASS` 证明 ES8311 ADC/I2S 输入有非零音频数据，不等于声学质量已评估。
- `Audio WARN` 是刻意保留的人耳确认项：听见 1kHz 与回放人声后，才能把放音链路升为 `[OK]` 并打开能力宏。

# [0.7.7] - 2026-08-09

把已经实测通过的外设路径收敛为正式板级接口。

### 变更

- 新增 `touch.h/.cpp`：提供 `bsp_touch_init()`、`bsp_touch_read()`、`bsp_touch_raw()` 与 `bsp_touch_monitor()`，使用 FT6336 官方复位时序和已验证的 `0x00..0x06` 坐标读取路径。
- 新增 `sdcard.h/.cpp`：提供 `bsp_sd_*` API，固定走 SD_MMC 1-bit，包含小文件写入/读回/删除校验。
- 新增 `audio_id.h/.cpp`：只提供 ES8311 I2C 身份寄存器读取，不打开音频输出能力宏。
- `bsp_battery_adc_mv()` / `bsp_battery_mv()` / `bsp_battery_level()` 不再是空桩；因分压比未标定，`bsp_battery_mv()` 返回 ADC pin mV，不伪造 VBAT。
- `BSP_HAS_TOUCH`、`BSP_HAS_SDCARD`、`BSP_HAS_BATTERY` 置 1；`BSP_HAS_AUDIO_OUT` 仍为 0。
- 自检中的 `ES8311`、`BatADC`、`SD` 项改走新的板级接口。
- `Touch4` 在无人操作时降级为 WARN：四角全中才 PASS，未触摸不再让无人值守自检变成 FAIL。

### 实测结果

- 烧录目标：ESP32-S3-PICO-1，8MB Flash + 8MB PSRAM，MAC `ac:27:6e:d1:e8:bc`。
- 开机 smoke test：右半屏触摸命中 `x199 y128`，显示从 `1` 刷新到 `2`。
- `bsp_touch_init()` 成功，后续 I2C 自检仍稳定扫描到 `18 38 51 70`，说明 `Wire` 与自检 I2C driver 切换没有破坏总线。
- `TouchP`：`n1 g00 x191 y117`；`Touch4`：`maskF`。
- `ES8311`：`id8311 ver01`；`BatADC`：`off2412/2014mV on2411/2013mV`；`SD`：`SDHC 29818MB rwOK`。
- 全局自检：`13 PASS / 2 WARN / 0 FAIL`。

### 边界

- 触摸坐标方向沿用实测 raw 坐标，未做旋转/手势事件队列。
- 电池电压百分比只是 ADC mV 的粗略映射，不代表真实电池电压百分比。
- ES8311 只证明 I2C 身份，不证明 I2S、PA 或喇叭链路已出声。

# [0.7.6] - 2026-08-09

完成一次手动外设 sweep，覆盖触摸四角、按钮动态、电池 ADC、ES8311 身份和 SD_MMC 读写。

### 变更

- `selftest.cpp` 增加手动页面提示：`Touch4` 四角触摸和 `Buttons` 动态按键窗口会刷到 EPD 上。
- 增加 `Touch4`：20 秒内采集四角触摸，按象限汇总 mask。
- 增加 `BtnDyn`：12 秒内等待 BOOT 与 PWR/BAT_KEY 被按下。
- 增加 `BatADC`：读取 `BAT_CTRL` 低/高两种状态下 `BAT_ADC` 的 raw/mV。
- 增加 `ES8311`：读取 `0xFD/0xFE/0xFF`，用 ES8311 手册期望 `0x83/0x11` 判定身份。
- 增加 `SD`：以 SD_MMC 1-bit 模式挂载 TF 卡，写入/读回/删除小文件。

### 实测结果

- `Touch4`：`maskF`，四角均命中；样例点包括 `x164 y168`、`x10 y39`、`x153 y70`、`x0 y177`。
- `ES8311`：`id 83 11 ver01`，自检 `PASS ES8311 id8311 ver01`。
- `BatADC`：`off raw2411 mv2016 on raw2409 mv2015`，自检 PASS。两档几乎相同，说明 IO17 未门控 ADC 分压采样域；这与此前 IO17 不是电池锁存切断脚的结论一致。
- `SD`：`29818MB rwOK`，写读删小文件成功。
- `BtnDyn`：`BOOT1 PWR1`，动态按压通过。
- 全局自检：`13 PASS / 2 WARN / 0 FAIL`。

### 边界

- `TouchP` 本轮未命中，报 `WARN no-press-seen`；但 `Touch4 maskF` 已覆盖触摸坐标路径，故不影响本轮四角判据。
- ES8311 只证明 I2C 身份寄存器匹配，不证明 I2S 音频链路出声。
- SD 卡通过裸 `SD_MMC` 读写验证，但尚未提供公共 `bsp_sd_*` API，`BSP_HAS_SDCARD` 仍不打开。
- 电池 ADC 已读到稳定 mV，但分压阻值未标定，仍不定义电池电压换算比例。

# [0.7.5] - 2026-08-09

触摸驱动可见 EPD 状态变化验证通过。

### 变更

- `epd_visible_smoke_test()` 在显示大号 `1` 后打开 15 秒触摸窗口。
- 用户按右半屏时，固件读取 FT6336 坐标并立即刷新为大号 `2`，随后进入常规自检。

### 实测结果

- 串口：`Touch page hit n1 x199 y124`。
- 串口：`touch-next digit2 display done busy1755ms x199 y124`。
- 后续触摸按压自检再次通过：`PASS TouchP n1 g00 x185 y126`。
- 全局自检：`9 PASS / 1 WARN / 0 FAIL`。

### 边界

- 这是交互 smoke test，不是正式分页 UI 框架。
- 右半屏判据只验证“触摸坐标可驱动可见刷新”；四角校准、事件队列、公共 `bsp_touch_*` API 仍未实现。

# [0.7.4] - 2026-08-09

FT6336 触摸按压坐标路径通过实机验证。

### 变更

- `selftest.cpp` 增加 `TouchP` 人工按压窗口：复位 FT6336 后轮询 8 秒，等待用户按住/点按屏幕。
- 一旦读到触点，校验触点数不超过 2，且坐标落在 200x200 EPD 面板范围内。

### 实测结果

- 串口：`TouchPress seen1 n1 g00 x165 y78 ok1 fail0`。
- 自检：`PASS TouchP n1 g00 x165 y78`。
- 全局自检：`9 PASS / 1 WARN / 0 FAIL`。

### 边界

- 本轮证明 FT6336 按压坐标数据路径成立。
- `BSP_HAS_TOUCH` 仍暂不打开：还没有公共 `bsp_touch_*` API、坐标方向/边界也只采到一个点，未做四角校准。

# [0.7.3] - 2026-08-09

FT6336 触摸控制器完成**空闲寄存器路径**分项验证。

### 变更

- `selftest.cpp` 增加 `Touch` 自检项：按官方时序复位 FT6336 后读取 `0x00..0x06`，解析 `TD_STATUS@0x02` 与 `P1_X/Y@0x03..0x06`。
- 同时尝试读取 `0xA3/0xA6/0xA8` 作为辅助 raw 证据，但不把它们作为身份判据。

### 实测结果

- 串口：`Touch raw00 00 00 00 00 00 00 00 idA3=00 A6=00 A8=00 int1`。
- 自检：`PASS Touch n0 g00 x0 y0 id00/00/00 int1`。
- 全局自检：`8 PASS / 1 WARN / 0 FAIL`。

### 边界

- 本轮证明 FT6336 I2C 寄存器可读、空闲触点数合法、INT 空闲为高。
- 本轮**未做人手按压坐标判读**，因此不把 `BSP_HAS_TOUCH` 置 1；触摸坐标和交互 API 仍是后续工作。
- `0xA3/0xA6/0xA8` 在本板读到 `00/00/00`，现阶段不得把这些寄存器当作 FT6336 身份寄存器。

# [0.7.2] - 2026-08-09

RTC 与 SHTC3 从“地址 ACK”升级为**寄存器/传感器数据实测通过**。

### 变更

- `selftest.cpp` 增加 PCF85063 RTC 寄存器读取：连续读取时间寄存器两次，校验 BCD 范围、OS 位和秒跳变。
- `selftest.cpp` 增加 SHTC3 数据读取：唤醒、读 ID、触发 non-stretching 温湿度转换，校验 ID/温度/湿度 CRC8，并检查数值范围。
- SHTC3 首轮使用 clock-stretching 测量命令超时，已改为 non-stretching 命令 `0x7866` + 显式等待 15ms。

### 实测结果

- RTC：`raw 12 00 09 27 03 08 25 -> 13 00 09 27 03 08 25 delta1 os0`，自检 `PASS RTC 09:00:13 d27 m08 y25 +1s os0`。
- SHTC3：`id0887 crc1 rawT6571 rawH99B2 T24.3 RH60.0 crc11`，自检 `PASS SHTC3 id0887 24.3C 60.0% crc111`。
- 全局自检：`7 PASS / 1 WARN / 0 FAIL`，唯一 WARN 仍为 EPD 可见性需人眼确认。

### 边界

- `BSP_HAS_TOUCH`、`BSP_HAS_BATTERY` 等能力宏不变；本版本只验证 RTC/SHTC3 的 I2C 数据路径。
- RTC 时间未做授时准确性判断，只证明 PCF85063 寄存器可读、BCD 合法、秒计数在走、OS 位未置位。

# [0.7.1] - 2026-08-09

首次确认本仓库固件在 ePaper-1.54 上完成**屏幕真实可见刷新**。

### 实测结果

- 烧录目标：ESP32-S3-PICO-1，8MB Flash + 8MB PSRAM，MAC `ac:27:6e:d1:e8:bc`。
- 串口：`digit1 display done busy1755ms`，随后 `UI flush done busy1755ms`。
- 用户目视确认：先看见大号 `1`，随后看见自检文本页。
- 自检结果：`5 PASS / 1 WARN / 0 FAIL`。EPD 仍保留 `WARN ... vis?`，因为当前自检接口还需要人眼确认可见性，尚未实现自动视觉判据。

### 变更边界

- `BSP_PIN_EPD_PWR_EN` 语义修正为 active-low：`ON=LOW, OFF=HIGH`。
- 最小可见入口命名为 `epd_visible_smoke_test()`；旧 `bsp_display_calibration_pattern()` 仅保留为兼容 wrapper。
- `bsp_ui_flush()` 已把 16x30 文本缓冲渲染为 200x200 1-bit framebuffer，并走已验证的 factory-style EPD 刷新路径。
- 本版本只证明 EPD 刷新路径和 UI flush 成立；RTC/SHTC3、触摸、按钮、电池 ADC 仍需后续分项验证，不与 EPD 调试混测。

# [0.6.2] - 2026-08-08

回刷原厂固件做交叉验证后的版本。**I2C 从全灭转为四器件应答**，自检首次出现 0 FAIL；EPD 仍未可见刷新。

### 起因：跳出「改一版、烧一版、还是不亮」的循环

v0.5.0 → v0.6.1 共四轮烧录，一直在症状层加仪表（电源/复位/频率探针、线电平诊断、bus-unwedge、
三段全刷），但从未验证过**参考实现**。本版先回刷 `backup_epaper154_factory_20260807.bin`
（8MB 全片，hash 校验通过），由用户目视确认**原厂固件点亮了屏幕**，一次性排除了「板子或面板损坏」
这一整个分支，并拿到原厂启动日志作为逐条可比对的硬证据。

### 变更

- `board.cpp` 增加 IO42 输出驱动，与原厂 GPIO 配置对齐（原厂在跑任何外设前把 IO6/IO17/IO42
  三根全部配成输出）。IO42 原理图叫 PA_EN，官方 `user_config.h` 归在 DEV POWER 段叫 Audio_PWR_PIN。
- `ui.cpp` 增加电源域探针：以 I2C 双线内部上拉回读作仪表，扫描 IO6/IO17/IO42 的 8 组电平组合，
  每组停留 1500ms，另做 IO42 单变量隔离复核两轮。**全部机器可判读，不需要人眼参与。**
- `ui.cpp` 增加 RTC 掉电判据（`RTC_NOINIT_ATTR` 魔数）：跨软复位保留、真掉电才丢失，
  输出 `BOOT COLD-power-was-lost` / `WARM-power-never-lost`。复位原因寄存器会被主机打开串口的
  动作顶掉，不可靠，故不用它。
- 修复 `epd_set_last()` 的 `snprintf` 源目标重叠 UB（init 失败路径会把 `s_epd_last_detail` 传回自身）。
- 修复 EPD init 失败时误报 detail：`s_epd_last_detail` 初值 `"not-run"` 使 `?:` 恒真，
  `"initfail"` 是死代码，失败时会把「没跑过」当成失败原因打印。
- `epd_init_panel()` 不再无条件拉高 IO6，改为沿用电源域探针选定的电平。
- SPI 降频值从 `bsp_pins.h` 挪到 `ui.cpp` 的 `EPD_SPI_FREQ_PROBE`：它是固件调参不是硬件事实，
  贴 `[OK]/[REF]/[DOC]` 任一标记都是造假值（status sync 检查拦下了这次越界，检查器判得对）。

### 实测结果

| 项 | v0.6.1 | v0.6.2 |
|---|---|---|
| 自检 | 4 PASS / 1 WARN / 1 FAIL | **5 PASS / 1 WARN / 0 FAIL** |
| I2C 线电平 | `raw SDA0 SCL0 \| pu SDA0 SCL0` | `raw SDA1 SCL1 \| pu SDA1 SCL1` |
| I2C 器件 | 全 miss，扫描 `skip bus-low` | `38 51 70`，全扫描另见 `18` |

应答器件：`0x38` FT6336 触摸、`0x51` PCF85063 RTC、`0x70` SHTC3、`0x18` ES8311 codec（预期外收获）。
100kHz 与 400kHz 均稳定。原厂日志的 `shtc3: ID:0887` 与 SD 卡 `29818MB SDHC` 独立佐证总线与卡座完好。

### 判读边界（重要）

- **I2C 的 PASS 未归因，不得升 `[OK]`。** 电源域探针显示 8 组电平组合下 I2C rail 全部为 UP，
  **包括 IO6/IO17/IO42 三根使能全低**，故这三根都没有门控 I2C 上拉域。v0.6.1 的 bus-low 到本版
  四器件应答，其间唯一发生过的事是跑了一遍原厂固件，因此**无法排除当前 PASS 依赖原厂固件残留状态**。
- 冷启动归因测试**未完成**：长按 PWR 键未能切断电池供电；固件侧反复拉低 IO17 亦未能切断
  （据此确认 **IO17 (BAT_Control) 不是电池锁存切断脚**，`bsp_power_off()` 空桩暂不填）。
  唯一剩余手段是拔电池插头，经权衡暂缓 —— RTC 掉电判据已常驻固件，将来任何一次真实掉电
  都会自动打印 `COLD`，归因届时自动完成。
- **EPD 仍未通过。** v0.6.2 中 I2C 电源域完全健康、四器件应答的同一轮里，EPD 三段全刷命令链照常完成、
  BUSY 照常 1755ms 返回，但用户确认屏上仍是原厂画面。**故 EPD 故障与电源域无关，是驱动路径自身问题。**
  EPD 自检维持 `WARN ... vis?`，引脚表仍无一 `[OK]`。

### 验证快照

| 项 | 值 |
|---|---|
| 验证日期 | 2026-08-08 |
| 硬件 | ESP32-S3-Touch-ePaper-1.54，MAC `ac:27:6e:d1:e8:bc`，ESP32-S3-PICO-1 rev v0.2 |
| 自检结果 | 5 PASS / 1 WARN / 0 FAIL |
| 证据 | `baseline/selftest_epaper_154_v0.6.2.log`、`baseline/pwr_attribution_epaper_154_v0.6.2.log`、`baseline/coldboot_epaper_154_v0.6.2.log`、`baseline/factory_epaper_154_boot.log` |

# [0.6.1] - 2026-08-08

EPD 最小全刷未见可见变化后的纯软件诊断版本。按用户要求，继续跳过万用表、示波器、逻辑分析仪等外部测量路径。

### 变更

- EPD SPI 刷新频率从 `BSP_EPD_SPI_FREQ` 的 40MHz 降到 4MHz，用于排除高速 SPI 边沿/布线余量导致的不可见刷新。
- 开机显示测试从单次棋盘/边框改为三段强对比全刷：全黑、全白、棋盘/边框；每段之间留 1200ms，便于肉眼观察闪烁或画面变化。
- 串口分别打印 `black` / `white` / `checker` 三段 BUSY 等待结果，并汇总为 `bw-checker-sent`。

### 判读边界

- 实测日志显示三段命令链均完成：`black done busy1754ms`、`white done busy1754ms`、`checker done busy1754ms`，总 BUSY 等待 `5262ms`。
- 用户随后两次目视确认：屏幕没有任何变化。因此本版不能认为 EPD 可见刷新成功；`checker-sent` / `bw-checker-sent` 只表示 MCU 侧发送与 BUSY 等待完成。
- EPD 自检仍保持 `WARN ... vis?`，不升级为 PASS，也不把任何 EPD 引脚改为 `[OK]`。
- I2C 仍为 bus-low：SDA/SCL 均为 0，全地址扫描继续 `skip bus-low`。

### 验证快照

| 项 | 值 |
|---|---|
| 验证日期 | 2026-08-08 |
| 硬件 | ESP32-S3-Touch-ePaper-1.54，MAC `ac:27:6e:d1:e8:bc` |
| 自检结果 | 4 PASS / 1 WARN / 1 FAIL |
| EPD 串口 | `black done busy1754ms`；`white done busy1754ms`；`checker done busy1754ms`；`bw-checker-sent busy5262ms` |
| EPD 目视 | 用户确认屏幕无任何变化 |
| I2C | `vbat0 100k: -`，`vbat1 100k: -`，`vbat1 400k: -`；全地址扫描 `skip bus-low` |
| 证据 | `baseline/selftest_epaper_154_v0.6.1.log` + 用户目视确认 |

# [0.6.0] - 2026-08-08

新增 EPD 最小可见全刷测试。由于这是从 `no-refresh` 骨架进入真实 SSD1681/SPI 初始化与刷新路径，按 draft 规则记为 MINOR。

### 变更

- `ui.cpp` 接入最小 EPD 驱动路径：SPI2、RST/DC/CS/BUSY 控制、官方 full-refresh LUT、200x200 framebuffer、棋盘/边框测试图。
- `bsp_display_calibration_pattern()` 现在会在开机自检前尝试一次全刷，并通过串口输出刷新开始、完成/失败、BUSY 等待耗时。
- EPD 自检项改为读取这次刷新尝试的结果：若命令链完成且 BUSY 返回，仍报 `WARN ... visual-check`，等待人工目视确认后才能升级判据；若 SPI/init/BUSY 超时失败，则报 FAIL。
- `board.cpp` 删除旧的 `bsp_display_calibration_pattern()` 空桩，由 `ui.cpp` 提供真实实现。

### 判读边界

- 实测日志显示 `EPD refresh done checker-sent busy1755ms`，说明 MCU 侧 SPI 命令链完成，BUSY 在 1755ms 后回到 idle。
- 这仍不等于屏幕可见刷新已经确认：墨水屏内容是否真的变成棋盘/边框图，需要肉眼确认。因此本版 EPD 仍保持 WARN，不升级任何 `[OK]`。
- I2C 诊断结论没有改善：SDA/SCL 在本次日志中均为 0，bus-unwedge 后仍 `SDA0 SCL0`，全地址扫描继续 `skip bus-low`。
- 已按用户要求跳过万用表、示波器、逻辑分析仪等外部测量路径；后续只做软件可执行或肉眼可确认的测试。

### 验证快照

| 项 | 值 |
|---|---|
| 验证日期 | 2026-08-08 |
| 硬件 | ESP32-S3-Touch-ePaper-1.54，MAC `ac:27:6e:d1:e8:bc` |
| 自检结果 | 4 PASS / 1 WARN / 1 FAIL |
| EPD | `refresh done checker-sent busy1755ms`；自检 `WARN ... visual-check` |
| I2C | `vbat0 100k: -`，`vbat1 100k: -`，`vbat1 400k: -`；全地址扫描 `skip bus-low` |
| 证据 | `baseline/selftest_epaper_154_v0.6.0.log` |

# [0.5.1] - 2026-08-08

I2C 失败后的电平与 bus-unwedge 诊断版本。只改自检诊断输出，不改引脚值、能力宏或外设承诺。

### 变更

- `selftest.cpp` 增加 I2C SDA/SCL 阶段电平诊断：`boot`、`epd_pwr_on`、`vbat_off`、`vbat_on`、`touch_reset`、`post_unwedge`。每阶段同时打印 raw 输入读数与内部上拉后读数。
- 增加 100kHz / 400kHz 全地址扫描；若 SDA/SCL 在内部上拉后仍未全部为高，则明确输出 `skip bus-low`，避免在已知 bus-low 状态下长时间等待每个地址超时。
- 增加 bus-unwedge：释放 I2C driver，将 SDA/SCL 配为开漏 + 内部上拉，手动给 SCL 16 个脉冲后发 STOP 形态，然后再测线状态与扫描。

### 判读边界

- 实测仍为 4 PASS / 1 WARN / 1 FAIL，与 v0.5.0 一致。
- SDA 在所有阶段都为 `0`；SCL 在部分阶段能被内部上拉读到 `1`，但 `touch_reset` / `post_unwedge` 等阶段仍可回到 `0`。bus-unwedge 输出 `SDA0 SCL1`，说明 SDA 未被释放。
- 全地址扫描因 `bus-low` 被跳过，本版没有新的 ACK 证据；不将任何 I2C 引脚或外设升级为 `[OK]`。
- 现在的主要结论不再是「地址未应答」，而是「至少 SDA 线处于低电平/不可释放状态」。下一步应以万用表/示波器/逻辑分析仪检查 GPIO47/48 和 I2C 上拉/电源域。

### 验证快照

| 项 | 值 |
|---|---|
| 验证日期 | 2026-08-08 |
| 硬件 | ESP32-S3-Touch-ePaper-1.54，MAC `ac:27:6e:d1:e8:bc` |
| 自检结果 | 4 PASS / 1 WARN / 1 FAIL |
| I2C 线状态 | `boot raw SDA0 SCL0 | pu SDA0 SCL1`；`touch_reset raw SDA0 SCL0 | pu SDA0 SCL0`；`unwedge done SDA0 SCL1` |
| I2C 探针 | `vbat0 100k: -`，`vbat1 100k: -`，`vbat1 400k: -`；全地址扫描 `skip bus-low` |
| 证据 | `baseline/selftest_epaper_154_v0.5.1.log` |

# [0.5.0] - 2026-08-07

I2C 首轮失败后的窄范围探针版本。只改电源/复位/频率采证，不接入触摸、RTC、温湿度驱动，也不改变能力宏。

### 变更

- `bsp_board_init()` 拉高 `BSP_PIN_BAT_CTRL`，对齐官方流程里的 `BoardPower_VBAT_ON()`：官方 wiki 在 I2C 设备初始化前依次执行 `BoardPower_EPD_ON()`、`BoardPower_Audio_ON()`、`BoardPower_VBAT_ON()`。
- FT6336 复位时序改为官方 demo 的 `HIGH 100ms -> LOW 100ms -> HIGH 100ms`。
- I2C 探针增加三组串口输出：`VBAT off + 100kHz`、`VBAT on + 100kHz`、`VBAT on + 400kHz`。自检汇总取 VBAT on 下更好的结果。

### 判读边界

- 这仍然只是 ACK 探针：地址出现只能说明 I2C 总线与器件响应存在，不能证明触摸坐标、RTC 走时、SHTC3 读数正确。
- 实测三组探针仍全 miss：`VBAT off + 100kHz`、`VBAT on + 100kHz`、`VBAT on + 400kHz` 均未扫到 `0x38` / `0x51` / `0x70`。
- 下一步应回到资料/原理图/原厂固件层复核 I2C 引脚、电源域和板载器件是否被版本差异改变，而不是继续盲目扩展软件探针。

### 验证快照

| 项 | 值 |
|---|---|
| 验证日期 | 2026-08-07 |
| 硬件 | ESP32-S3-Touch-ePaper-1.54，MAC `ac:27:6e:d1:e8:bc` |
| 自检结果 | 4 PASS / 1 WARN / 1 FAIL |
| I2C 探针 | `vbat0 100k: -`，`vbat1 100k: -`，`vbat1 400k: -` |
| 证据 | `baseline/selftest_epaper_154.log` |

# [0.4.0] - 2026-08-07

首轮实机前预备固件。目标不是证明墨水屏已可显示，而是让换板后的第一份基础硬件证据可观测、可留档。

**级别判定：本应 MAJOR ②（已发布板级身份/构建参数修正），因 draft 降为 MINOR，
见 VERSIONING.md §3.1.1。**

### 撤回

| 参数 | 旧值 | 新值 | 撤回理由 |
|---|---|---|---|
| `BSP_BOARD_MODULE` | `ESP32-S3-WROOM-1` | `ESP32-S3-PICO-1-N8R8` | 2026-08-07 `esptool flash_id` 实测为 ESP32-S3-PICO-1，Embedded Flash 8MB + Embedded PSRAM 8MB |

### 新增

- `ui.cpp` 的 `bsp_ui_printf()` 增加串口镜像，且串口使用 128 字节本地缓冲，不跟随 32 字节屏幕行缓冲截断。此前自检结果只进内存缓冲，`bsp_ui_flush()` 又尚未接真实墨水屏驱动，导致烧录后串口只能看到版本行，看不到任何自检结论。
- 新增 `selftest.cpp`，首轮只跑 6 项：PSRAM / Flash / EPD 控制脚 / I2C / WiFi / Btn。
- I2C 只探预期三地址（FT6336/PCF85063/SHTC3），使用 ESP-IDF `i2c_master_cmd_begin(..., 20ms timeout)`，避免首轮未知总线状态上做全地址扫描导致自检卡死。
- `bsp_board_init()` 初始化 EPD 供电与控制脚、触摸复位/中断脚、RTC 中断脚、BOOT/PWR 输入。
- `BSP_I2C_FREQ=400000`，作为首轮 I2C 扫描频率，待实机确认后再转 `[OK]`。
- `platformio.ini` 的 `epaper_154` 环境补齐 8MB Flash / OPI PSRAM 构建参数。此前编译输出显示 `No PSRAM`，与实机 `ESP32-S3-PICO-1-N8R8` 不符。

### 验证快照

| 项 | 值 |
|---|---|
| 验证日期 | 2026-08-07 |
| 硬件 | ESP32-S3-Touch-ePaper-1.54，MAC `ac:27:6e:d1:e8:bc` |
| 识别工具 | esptool.py v4.11.0 via PlatformIO `tool-esptoolpy` |
| 芯片识别 | ESP32-S3-PICO-1 rev v0.2，Embedded Flash 8MB (GD)，Embedded PSRAM 8MB |
| 原厂备份 | `backup_epaper154_factory_20260807.bin`，8388608 bytes，SHA256 `E2BC675517B9C0F498BABB8B59E5ABC45994140088A059F0EEC515E2DD02BEDB` |
| 首轮自检 | 4 PASS / 1 WARN / 1 FAIL |
| 证据 | `baseline/selftest_epaper_154.log` |

### 判读边界

- EPD 项目前报 `WARN ... no-refresh` 是刻意的：它只确认 PWR/RST/CS 这几个 MCU 侧控制脚处在可测试状态，**不等于 SSD1681 初始化成功，也不等于屏幕可见刷新成功**。
- 本版不改 `BSP_HAS_TOUCH` / `BSP_HAS_SDCARD` / `BSP_HAS_AUDIO_*` / `BSP_HAS_BATTERY`。这些外设虽然已有引脚资料，但还没有对应 `bsp_*` 实现，仍不进入能力宏。

### 未验证 / 遗留

- 尚未有任何 `[OK]` 标记变更：首轮只证明固件可烧录、串口可观测、PSRAM/Flash/WiFi/按钮空闲读数可用。
- I2C 首轮未通过：预期 `0x38` / `0x51` / `0x70` 均无 ACK。后续需确认是否还有未拉高的外设电源/复位脚、I2C 引脚来源是否需复核、或是否需要降低频率到 100kHz。
- ePaper 显示刷新、BUSY 刷新中极性、触摸坐标、SD_MMC、RTC、SHTC3、电池 ADC、音频均未验证。

# [0.3.0] - 2026-08-06

取到官方 Demo 仓库 `github.com/waveshareteam/ESP32-S3-ePaper-1.54`（可信度阶梯第 3 层），
与原理图互证。**v0.2.1 记录的三条「已知缺口」一次全部解决**，其中两条的结论
与当初的提法相反 —— 不是补上了答案，是问题问错了方向。

**级别判定：本应 MAJOR ②（已发布引脚宏的语义修正），因 draft 降为 MINOR，
见 VERSIONING.md §3.1.1。GPIO 号一个都没变，变的是名字与总线形态。**

### 撤回

| 参数 | 旧值 | 新值 | 撤回理由 |
|---|---|---|---|
| BSP_PIN_SD_MISO | 40 | **改名** `BSP_PIN_SD_D0` = 40 | 本板 SD 走 SD_MMC 1-bit (SDIO) 而非 SPI，40 是 D0 不是 MISO |
| BSP_PIN_SD_MOSI | 41 | **改名** `BSP_PIN_SD_CMD` = 41 | 同上，41 是 CMD 不是 MOSI |

**这两条属于「来源错」，不是「推断错」。** 资料本身把话说歪了：原理图 U8 IO 映射表
就是把 IO40/IO41 标成 `SD_MISO` / `SD_MOSI` 的，SPI 味的命名。而官方驱动
`04_SD_Card/sdcard_bsp.cpp` 用 `sdmmc_slot_config_t` 且 `slot_config.width = 1`，
三根线是 CLK / CMD / D0。**信号名里的 MISO/MOSI 推不出总线形态**
（SOURCE_HARVESTING §4.3）。`lcd_085` 在同源的坑里栽过 —— 那次是官方文档把
SDIO 写成 SPI，照着写怎么都初始化不了。

### 已解决的缺口（v0.2.1 §3「已知缺口」三条）

| v0.2.1 的提法 | v0.3.0 的结论 |
|---|---|
| SD_CS 归属未明，需进一步查证 | **本板没有 SD_CS。** SDIO 总线上不存在这根线；原理图 SD_CS 处的 `R41 NC` 本身就是答案，不是待解的线索 |
| 触摸 IC 型号与 I2C 地址未明示 | **FT6336 @ 0x38**，INT=IO21 / RST=IO7（示例 `12_FT6336_Test/`） |
| LUT 波形未知，推测走 SSD1681 OTP 内置 | **不是 OTP。** 官方驱动自带两张表 `WF_Full_1IN54[159]` 与 `WF_PARTIAL_1IN54_0[159]`，经 `EPD_SetLut()` 下发。推测错了 |

前两条的价值不在答案本身，在于**缺口清单上写着的问题可能连问题都不成立**。
SOURCES.md §4.4 保留了原文与更正，没有删掉。

### 新增

- `BSP_TOUCH_I2C_ADDR` = 0x38、`BSP_RTC_I2C_ADDR` = 0x51、`BSP_SHTC3_I2C_ADDR` = 0x70
  （示例 `user_config.h`，三个器件挂同一条 I2C）。
- `BSP_SD_BUS_WIDTH` = 1（示例 `sdcard_bsp.cpp` `slot_config.width = 1`）。
- `BSP_EPD_SPI_FREQ` = 40000000。⚠️ 官方源码里这一行的注释写的是 10 MHz，
  与代码不符 —— **取代码值**，注释不会被编译。冲突已记入 SOURCES §4.6。
- `docs/refs/demo/` 入库四个官方源文件：`user_config.h`、`sdcard_bsp.cpp`、
  `epaper_driver_bsp.cpp`、`ft6336_bsp.cpp`（照 SOURCE_HARVESTING §5.1
  「示例包里**实际引用过**的少数几个源文件」）。

### 可信度提升（值未变）

`BSP_EPD_BUSY_ACTIVE_HIGH` 与 `BSP_EPD_PARTIAL_8PIXEL_ALIGN` 此前只有 Datasheet
一个来源，现在官方驱动独立吻合：
- `read_busy()` 是 `while(gpio_get_level(busy) == 1)`，注释 `//LOW: idle, HIGH: busy`；
- `EPD_SetWindows()` 下发 0x44 时写 `(Xstart>>3)`，像素寻址 `index = y*25 + (x>>3)`，
  源码注释 `//25是200/8`。

BUSY 写反会刷屏死等，8 像素对齐是 `bsp_ui_status()` 行定位抽象的架构前提 ——
这两条各拿到第二个独立来源，是本次最有分量的收获。
**但按 SOURCE_HARVESTING §1，互证不升级为 `[OK]`，只有实机可以。标记仍是 `[REF]`。**

### 未验证 / 遗留

- **`BSP_HAS_TOUCH` 仍为 0。** FT6336 的型号、地址、两个引脚现在全都取证到位，
  但本板没有任何 `bsp_touch_*` 实现。**取证到型号不等于有支持**
  （SOURCE_HARVESTING §4.7；amoled_206 v0.3.0 正是在这里栽的）。
- `BSP_EPD_BUSY_ACTIVE_HIGH` / `BSP_EPD_PARTIAL_8PIXEL_ALIGN` 目前**零使用点**。
  `ui.cpp` 60 行、`board.cpp` 29 行都还是骨架。现在拿到官方驱动了，
  接驱动的前置条件已满足 —— 但那是下一版的事，本版只做取证。
- 全刷/局刷的实际耗时（毫秒）只能实测。
- 电池分压电阻阻值原理图未标，仍不定义 `BSP_BAT_ADC_RATIO`。
- 无实物，全表无一 `[OK]`，成熟度维持 `draft`。
- FT6336 与 SSD1681 的完整数据手册未取，寄存器映射目前只有官方示例一个来源。

# [0.2.1] - 2026-08-06

审查 v0.2.0 那一轮取证时发现三处**记载与代码不符**，本条目只做更正，不动任何引脚值。

**级别判定**：PATCH ①（文档措辞与不实记载更正；引脚值、能力宏、接口行为均未变动）。

### 更正

- **v0.2.0 的「新增」段声称补充了 `BSP_HAS_TOUCH=1`、`BSP_HAS_AUDIO_OUT=1`、
  `BSP_HAS_SDCARD=1`、`BSP_HAS_BATTERY=1` 四个能力宏 —— 这件事从未发生。**
  该次提交对 `bsp_caps.h` 的改动只有版本号一行，这四个宏至今全是 `0`。
  **代码是对的**（接口未实现就该是 0），错的是 CHANGELOG。该行已删除并留注记。
- `docs/SOURCES.md` §2 跟着写「`bsp_caps.h` 中 `BSP_HAS_TOUCH=1` 仅代表引脚与外设存在」，
  同样与代码不符，且把能力宏的语义说错了。已改为：维持 `0`，并援引 VERSIONING.md §2.1
  「有此外设**且 `bsp_*` 接口已实现**」。
- `docs/hardware_config.md` 同一处误述（四个宏「=1」+「draft 阶段含义是有外设+引脚已取证」）
  一并改正，并写明 `[REF]` 说的是引脚取证，与能力宏是两回事。

> 三处文档说同一件不存在的事，而 `bsp_caps.h` 一直是对的 —— 典型的真值漂移：
> 事实写在四个地方，改代码时只有代码是准的。

### 变更

- `docs/SOURCES.md` §2「官方 Demo 代码包」一行改写。原文记「未找到官方打包的 Demo zip 链接」，
  字面没说谎（确实不是 zip），但读起来像「没有官方示例代码」。实际 Resources 页第 3 节
  白纸黑字列着「ESP32-S3-ePaper-1.54 Demo (GitHub)」（见 `docs/refs/wiki-text/wiki_05_resources.txt`）。
  按 SOURCE_HARVESTING §4.1，**抓取失败/未去取只能记「没拿到」，不能写成资料不存在**。
  已改为明确标注「存在，本轮未取」，并写明它是可信度阶梯第 3 层、
  三个已知缺口很可能都在其中。
- §2「电池分压电阻准确阻值」一行补上不定义 `BSP_BAT_ADC_RATIO` 的理由（§4.5 缺就缺着）。

### 未验证 / 遗留

- **下一轮必须补取官方 GitHub Demo** —— 同批次的 amoled_206 正是靠在线读该仓库的
  `pin_config.h` 完成原理图互证的，两块板对同类资料用了不同标准。
- SD_CS 归属、触摸 IC 型号与 I2C 地址、LUT 波形表三项仍是缺口。
- 本板无实物，全表无一 `[OK]`，成熟度停在 draft。

# [0.2.0] - 2026-08-06

**级别判定：本应 MAJOR ②（引脚值修正），因 draft 降为 MINOR，见 VERSIONING.md §3.1.1。**

以官方原理图 ESP32-S3-Touch-ePaper-1.54-Schematic 及官方 Datasheet 1.54inch e-Paper V2 为基准重建全部引脚与关键逻辑参数。

### 撤回

| 参数 | 旧值 | 新值 | 撤回理由 |
|---|---|---|---|
| BSP_PIN_EPD_BUSY | 13 | 8 | 8 在原理图 U8 映射表上是 EPD_BUSY，13 是 EPD_SDI |
| BSP_PIN_EPD_RST | 12 | 9 | 9 在原理图上是 EPD_RST，12 是 EPD_SCLK |
| BSP_PIN_EPD_DC | 11 | 10 | 10 在原理图上是 EPD_D/C，11 是 EPD_CS |
| BSP_PIN_EPD_CS | 10 | 11 | 11 在原理图上是 EPD_CS，10 是 EPD_D/C |
| BSP_PIN_EPD_CLK | 9 | 12 | 12 在原理图上是 EPD_SCLK |
| BSP_PIN_EPD_DIN | 8 | 13 | 13 在原理图上是 EPD_SDI |

**旧值为典型的错误配置（引脚逆序与错位）。**
`bsp_pins.h` v0.1.0 的 7 个宏标注为 `[DOC]`，无权威来源依凭。

### 新增

- EPD 供电开关：`BSP_PIN_EPD_PWR_EN=6` (IO6 EPD3V3_EN)。
- EPD 逻辑控制特性：
  - `BSP_EPD_BUSY_ACTIVE_HIGH=1`：Datasheet 确认为高有效 (High=Busy)。
  - `BSP_EPD_PARTIAL_8PIXEL_ALIGN=1`：Datasheet 确认 RAM X 寻址 (0x44) 必须按 8 像素/1 字节对齐。
- I2C 总线与外设：`SDA=47`, `SCL=48`；触摸屏 `TOUCH_RST=7`, `TOUCH_INT=21`；RTC `RTC_INT=5`。
- Micro SD (TF) 卡：`SD_CLK=39`, `SD_MISO=40`, `SD_MOSI=41`。
- 音频 (I2S Codec ES8311 + PA)：`I2S_MCLK=14`, `SCLK=15`, `ASDOUT=16`, `LRCK=38`, `DSDIN=45`, `PA_EN=42`, `PA_CTRL=46`。
- 电源/电池：`BAT_ADC=4`, `BAT_CTRL=17`, `BAT_KEY=18`。

> ⚠️ **本条目原有一行不实记载，v0.2.1 已删除**：曾写「`BSP_HAS_TOUCH=1`、`BSP_HAS_AUDIO_OUT=1`、
> `BSP_HAS_SDCARD=1`、`BSP_HAS_BATTERY=1` 能力定义补充」，而本次提交对 `bsp_caps.h`
> 的改动只有版本号一行，这四个宏至今全是 `0`。详见 v0.2.1 的「更正」段。

### 变更

- `docs/SOURCES.md` 全面补全：11 项已获取资料及本地路径登记，补全参数来源对照表及 §4 存疑与冲突分析。
- `docs/hardware_config.md` 扩充为完整外设验证总表与实机验证指南。
- `docs/refs/archive.json` 刷新，填充缺失的 url/kind/note 字段。
- `bsp_caps.h` 版本号更新为 v0.2.0。

### 未验证 / 遗留

- **全部**。本板无实物，全表无一 `[OK]`，成熟度停在 draft。
- SD_CS 选通引脚未在原理图 U8 映射表中明确标记，记入已知缺口。
- 触摸屏 IC 型号及 I2C Slave Address 未标明。

# [0.1.0] - 2026-08-05
- 骨架建立 (draft)
