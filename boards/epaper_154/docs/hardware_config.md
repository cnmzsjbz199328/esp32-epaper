# ESP32-S3-ePaper-1.54 硬件说明

参数来源与撤回记录见 [SOURCES.md](SOURCES.md)。本文件只记录**外设验证状态**。

## 外设验证总表

最近一次实机验证：**2026-08-09，v0.7.9 音频听感确认，音频能力宏已打开。**

下表的 `[REF]` 项仍只表示「原理图/数据手册官方一手来源已核对」，**不等于实机跑通过**。

- **原厂固件已在本板点亮屏幕**（用户目视确认），面板/FPC/EPD 供电域/SPI 走线全部完好，硬件损坏分支已排除。原厂启动日志见 `baseline/factory_epaper_154_boot.log`。
- **I2C 四器件应答**：`0x38` FT6336、`0x51` PCF85063、`0x70` SHTC3、`0x18` ES8311，100kHz/400kHz 均稳。
  ⚠️ **但此 PASS 未归因，不得升 `[OK]`** —— 电源域探针显示 IO6/IO17/IO42 三根使能全低时 I2C rail 仍为 UP，说明它们都没门控该域；v0.6.1 的 bus-low 到本版全通之间唯一发生过的事是跑了一遍原厂固件，无法排除当前 PASS 依赖其残留状态。冷启动归因测试因无法切断电池供电而暂缓（IO17 已证实不是锁存切断脚），RTC 掉电判据已常驻固件，将来任一次真实掉电会自动完成归因。
- **FT6336 触摸路径通过单点按压验证**：按官方时序复位后可读 `0x00..0x06`，空闲触点数 `n0` 合法，INT 空闲为高；人工按压读到 `n1 x165 y78`，坐标落在 200x200 面板范围内。v0.7.7 已提供 `bsp_touch_*` 轮询 API；坐标旋转/事件队列仍未实现。
- **触摸可驱动 EPD 可见刷新**：开机 smoke test 显示 `1` 后，右半屏触摸命中 `x199 y124`，随后刷新为 `2`；这证明触摸坐标可进入显示状态机。
- **手动 sweep 通过**：触摸四角 `maskF`，ES8311 chip-id `0x83/0x11`，SD_MMC 1-bit 读写 29818MB TF 卡成功，BOOT/PWR 动态按压通过，BAT_ADC 读到约 2015mV。
- **音频专项测试已完成闭环确认**：ES8311 初始化成功，录音完整读到 `48000` samples，`peak19340 avg748`，录音样本完整回放写入 I2S TX；用户已确认听见测试音与录音回放。
- **RTC/SHTC3 数据路径通过**：PCF85063 时间寄存器 BCD 合法、秒计数跳变、OS=0；SHTC3 读出 ID `0x0887`，ID/温度/湿度 CRC 均通过，读数 24.3C / 60.0%。
- **EPD 可见刷新已通过人工确认**：本仓库固件先显示大号 `1`，随后 `bsp_ui_flush()` 显示自检文本页；两次刷新 BUSY 均约 1755ms。当前 EPD 自检仍保留 `WARN ... vis?`，表示“需要人眼确认可见性”，不是命令链失败。
- 引脚表仍保持审慎标记，`maturity` 维持 `draft`；v0.7.9 已打开 `BSP_HAS_TOUCH`、`BSP_HAS_SDCARD`、`BSP_HAS_BATTERY`、`BSP_HAS_AUDIO_OUT`、`BSP_HAS_AUDIO_IN`。

| 外设 | 器件 | 标记 | 状态 |
|---|---|---|---|
| EPD 引脚 | 1.54" 黑白 Eink (SSD1681) | `[REF]` | ✅ 原理图 U8 IO 映射表确认 (BUSY8/RST9/DC10/CS11/CLK12/DIN13/PWR_EN6) |
| EPD 极性/对齐 | BUSY 高有效 / 局刷 8px 对齐 | `[REF]` | ✅ Datasheet 明确 BUSY High=Busy, 0x44 寄存器以 1 字节为单位 |
| 触摸屏 | EPD TP (I2C) | `[OK]` | ✅ FT6336 空闲寄存器、单点按压、四角坐标路径实测通过；v0.7.7 提供 `bsp_touch_*` 轮询 API |
| RTC | PCF85063 (I2C) | `[OK]` | ✅ 实机读取时间寄存器，两次读数秒跳变 +1s，BCD 合法，OS=0 |
| 温湿度 | SHTC3 (I2C) | `[OK]` | ✅ 实机读取 ID `0x0887`，温湿度 24.3C / 60.0%，CRC 全通过 |
| 音频 | ES8311 I2S Codec + PA | `[OK]` | ✅ ES8311 身份通过；I2S 录音 got48000/peak19340/avg748，回放写满 48000 samples；用户确认听见测试音与录音回放 |
| 按键 | BOOT + BAT_KEY/PWR | `[OK]` | ✅ 动态按压窗口内 BOOT 与 PWR/BAT_KEY 均检出 |
| 电源/电池 | 锂电池充电 + ADC 检测 | `[REF]` | ✅ BAT_ADC 读到约 2015mV；v0.7.7 提供 ADC mV API，分压阻值/电池电压换算未标定 |
| SD 卡 | SD_MMC 1-bit TF 卡槽 | `[OK]` | ✅ 1-bit SD_MMC 挂载 29818MB，写/读/删小文件通过 |
| Flash | ESP32-S3-PICO-1-N8R8 | — | ✅ `esptool flash_id` 确认 Embedded Flash 8MB；自检读出 8.0MB |
| PSRAM | ESP32-S3-PICO-1-N8R8 | — | ✅ `esptool flash_id` 确认 Embedded PSRAM 8MB；自检读写 1023KB 通过 |

⚠️ 上表的 `[REF]` 说的是**引脚已取证**，与能力宏是两回事。`bsp_caps.h` 里
`BSP_HAS_DISPLAY=1`、`BSP_HAS_TOUCH=1`、`BSP_HAS_SDCARD=1`、`BSP_HAS_BATTERY=1`。
`BSP_HAS_AUDIO_OUT=1`、`BSP_HAS_AUDIO_IN=1`：ES8311 I2S 机器路径与人耳听感均已确认。

> 本节此前写着这四个宏「=1」并解释成「有外设 + 引脚已取证」—— 那是对能力宏语义的
> 误述，且与 `bsp_caps.h` 的实际取值不符。v0.2.1 已改正，见 CHANGELOG。
> 参照 BOARD_MATRIX.md 给 `touch_lcd_154` 触摸列写的那条脚注：**有外设不等于有支持。**

## 拿到实物后怎么验证

1. **墨水屏 BUSY 状态等待**：
   - 验证 BUSY (IO8) 电平：空闲为 Low，刷新中为 High。
   - 刷屏超时或卡死首查 BUSY 极性是否被错误反转。

2. **墨水屏 3.3V 电源开关 (EPD3V3_EN)**：
   - IO6 为 active-low，需拉低使能 EPD 供电 LDO，刷新前确保 IO6 为低电平。

3. **局刷区域 8 像素对齐**：
   - 局刷 Window X 轴坐标需按 8 字节/8 像素向下对齐，检查 `ui.cpp` 的 `bsp_ui_status()` 逻辑。

4. **I2C 挂载扫描 (SDA 47 / SCL 48)**：
   - 扫 0x68 (PCF85063)、0x70 (SHTC3) 及 触摸 IC 地址，验证 I2C 总线工作正常。

5. **电池 ADC 采样标定**：
   - 测量 IO17 控制脚拉高后 IO4 的 ADC 读数，推算实际分压阻值与电压映射关系。

每确认一项，把 `bsp_pins.h` 对应行的 `[REF]`/`[DOC]` 改成 `[OK]` 并回填本表和 CHANGELOG。
