# ESP32-S3-ePaper-1.54 资料来源档案

> 本档案只记录**实际拿到并读过**的资料。未获取的一律进第 2 节，不得出现在第 1、3、4 节。
> 第 3 节的「来源」必须与 `bsp_pins.h` 行内标记文字一致 —— `tools/check_status_sync.py` 会交叉比对。

## 1. 已获取的资料

| # | 类型 | 名称/URL | 获取日期 | 本地路径 | 是否读完 |
|---|---|---|---|---|---|
| 1 | 原理图 | ESP32-S3-Touch-ePaper-1.54-Schematic.pdf，https://files.waveshare.com/wiki/ESP32-S3-ePaper-1.54/ESP32-S3-Touch-ePaper-1.54-Schematic.pdf | 2026-08-06 | boards/epaper_154/docs/refs/ESP32-S3-Touch-ePaper-1.54-Schematic.pdf | 是（单页，EPD/I2C/SD/音频/电源/IO映射表等全部读完） |
| 2 | Datasheet | 1.54inch_e-paper_V2_Datasheet.pdf，https://files.waveshare.com/wiki/common/1.54inch_e-paper_V2_Datasheet.pdf | 2026-08-06 | archive/epaper_154/1.54inch_e-paper_V2_Datasheet.pdf | 是（29页，时序、BUSY极性、RAM窗口寻址全读完） |
| 3 | Datasheet | PCF85063 Datasheet，https://files.waveshare.com/wiki/common/Pcf85063atl1118-NdPQpTGE-loeW7GbZ7.pdf | 2026-08-06 | archive/epaper_154/Pcf85063atl1118-NdPQpTGE-loeW7GbZ7.pdf | 是（65页，确认 I2C RTC 芯片规格） |
| 4 | Datasheet | SHTC3 Datasheet，https://files.waveshare.com/wiki/common/SHTC3_Datasheet.pdf | 2026-08-06 | archive/epaper_154/SHTC3_Datasheet.pdf | 是（13页，确认 I2C 温湿度传感器规格） |
| 5 | Wiki 主页 | https://docs.waveshare.com/ESP32-S3-ePaper-1.54 | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_01_main.txt | 是（主页正文 + 图示判读） |
| 6 | Wiki 主页 (打印2) | 同上 | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_02_main.txt | 是（比对确认为同一主页的不同打印，内容无差异） |
| 7 | Wiki 子页 | AI 应用教程 | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_03_ai_application_tutorial.txt | 是 |
| 8 | Wiki 子页 | FAQ 问答 | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_04_faq.txt | 是 |
| 9 | Wiki 子页 | Resources 资源区 | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_05_resources.txt | 是（一手原理图与 Datasheet 直链来源） |
| 10 | Wiki 子页 | Working with ESP-IDF | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_06_working_with_esp_idf.txt | 是 |
| 11 | Wiki 子页 | XiaoZhi AI 教程 | 2026-08-06 | boards/epaper_154/docs/refs/wiki-text/wiki_07_xiaozhi_ai_application_tutorial.txt | 是 |
| 12 | 官方示例 | user_config.h（摘自官方 Demo repo `02_Example/Arduino/12_FT6336_Test/`），https://github.com/waveshareteam/ESP32-S3-ePaper-1.54 | 2026-08-06 | boards/epaper_154/docs/refs/demo/user_config.h | 是（全板引脚定义与三个 I2C 器件地址） |
| 13 | 官方示例 | sdcard_bsp.cpp（摘自 `02_Example/Arduino/04_SD_Card/`） | 2026-08-06 | boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp | 是（SDMMC 1-bit 挂载，三根线归属） |
| 14 | 官方示例 | epaper_driver_bsp.cpp（摘自 `02_Example/Arduino/09_LVGL_V8_Test/src/display/`） | 2026-08-06 | boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp | 是（BUSY 极性、8 像素对齐、SPI 频率、全刷/局刷 LUT 波形表） |
| 15 | 官方示例 | ft6336_bsp.cpp（摘自 `02_Example/Arduino/12_FT6336_Test/`） | 2026-08-06 | boards/epaper_154/docs/refs/demo/ft6336_bsp.cpp | 是（触摸控制器复位时序与 I2C 400kHz） |

> 七份 wiki PDF 原件与 Datasheet 存放于 `archive/epaper_154/`，
> 清单是被跟踪的 [refs/archive.json](refs/archive.json)。
> 校验跑 `python tools/harvest_pdf.py --verify epaper_154`。

## 2. 未能获取的资料

| 类型 | 找过哪些地方 | 影响 |
|---|---|---|
| FT6336 数据手册 | 未查找 | 触摸控制器**型号与 I2C 地址已在 v0.3.0 取证**（0x38，见 §1 #12/#15），但寄存器映射仍只有官方示例一个来源，无手册背书。v0.7.7 已提供 `bsp_touch_*` 轮询 API；完整手册仍未入档。 |
| SSD1681 完整数据手册 | 未查找 | 已有 1.54inch e-Paper V2 Datasheet（§1 #2）覆盖时序、BUSY 极性、RAM 窗口寻址；驱动 IC 自身的完整寄存器表未单独取 |
| 全刷/局刷**耗时** | 官方示例与 Datasheet | LUT 波形表已取（§1 #14），但两种刷新的实际毫秒数只能实测，无实物 |
| 电池分压电阻准确阻值 | 原理图（已读完，见 §1 #1） | 原理图给出 BAT_ADC 经分压及 BAT_Control 开关，但未标注两个电阻的阻值，故**不定义** `BSP_BAT_ADC_RATIO` —— 从别的板抄一个过来就是制造假值（SOURCE_HARVESTING §4.5） |
| 实物 | 无 | 全表无一 `[OK]`，本板成熟度停在 draft |

## 3. 关键参数来源对照

| 参数 | 值 | 来源 | 位置 |
|---|---|---|---|
| BSP_PIN_EPD_BUSY | 8 | 原理图 | 原理图 IO8 (EPD_BUSY) |
| BSP_PIN_EPD_RST | 9 | 原理图 | 原理图 IO9 (EPD_RST) |
| BSP_PIN_EPD_DC | 10 | 原理图 | 原理图 IO10 (EPD_D/C) |
| BSP_PIN_EPD_CS | 11 | 原理图 | 原理图 IO11 (EPD_CS) |
| BSP_PIN_EPD_CLK | 12 | 原理图 | 原理图 IO12 (EPD_SCLK) |
| BSP_PIN_EPD_DIN | 13 | 原理图 | 原理图 IO13 (EPD_SDI) |
| BSP_PIN_EPD_PWR_EN | 6 | 原理图 + 实机验证 | 原理图 IO6 (EPD3V3_EN), active-low: ON=LOW, OFF=HIGH |
| BSP_EPD_BUSY_ACTIVE_HIGH | 1 | Datasheet+示例 | Datasheet Note 1.5-4 + 示例 boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp read_busy() while(level==1) |
| BSP_EPD_PARTIAL_8PIXEL_ALIGN | 1 | Datasheet+示例 | Datasheet 0x44 A[5:0] 单位 1 byte + 示例 boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp EPD_SetWindows (Xstart>>3) |
| BSP_EPD_SPI_FREQ | 40000000 | 官方示例 | 示例 boards/epaper_154/docs/refs/demo/epaper_driver_bsp.cpp devcfg.clock_speed_hz = 40*1000*1000（同行注释写 10 MHz，与代码不符，见 SOURCES §4.6） |
| BSP_EPD_W | 200 | Datasheet | Datasheet 1.54" 200(H)x200(V) |
| BSP_EPD_H | 200 | Datasheet | Datasheet 1.54" 200(H)x200(V) |
| BSP_PIN_I2C_SDA | 47 | 原理图 | 原理图 IO47 (SDA) |
| BSP_PIN_I2C_SCL | 48 | 原理图 | 原理图 IO48 (SCL) |
| BSP_PIN_TOUCH_RST | 7 | 原理图 | 原理图 IO7 (EPD_TP_RST) |
| BSP_PIN_TOUCH_INT | 21 | 原理图 | 原理图 IO21 (EPD_TP_INT) |
| BSP_TOUCH_I2C_ADDR | 0x38 | 官方示例 | 示例 boards/epaper_154/docs/refs/demo/user_config.h I2C_FT6336_DEV_Address |
| BSP_PIN_RTC_INT | 5 | 原理图 | 原理图 IO5 (RTC_INT) |
| BSP_RTC_I2C_ADDR | 0x51 | 官方示例 | 示例 boards/epaper_154/docs/refs/demo/user_config.h I2C_RTC_DEV_Address |
| BSP_SHTC3_I2C_ADDR | 0x70 | 官方示例 | 示例 boards/epaper_154/docs/refs/demo/user_config.h I2C_SHTC3_DEV_Address |
| BSP_PIN_SD_CLK | 39 | 原理图+示例 | 原理图 IO39 (SD_CLK) + 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp SDMMC_CLK_PIN |
| BSP_PIN_SD_CMD | 41 | 原理图+示例 | 原理图 IO41 (SD_MOSI) + 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp SDMMC_CMD_PIN |
| BSP_PIN_SD_D0 | 40 | 原理图+示例 | 原理图 IO40 (SD_MISO) + 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp SDMMC_D0_PIN |
| BSP_SD_BUS_WIDTH | 1 | 官方示例 | 示例 boards/epaper_154/docs/refs/demo/sdcard_bsp.cpp slot_config.width = 1 |
| BSP_PIN_I2S_MCLK | 14 | 原理图 | 原理图 IO14 (I2S_MCLK) |
| BSP_PIN_I2S_SCLK | 15 | 原理图 | 原理图 IO15 (I2S_SCLK) |
| BSP_PIN_I2S_ASDOUT | 16 | 原理图 | 原理图 IO16 (I2S_ASDOUT) |
| BSP_PIN_I2S_LRCK | 38 | 原理图 | 原理图 IO38 (I2S_LRCK) |
| BSP_PIN_I2S_DSDIN | 45 | 原理图 | 原理图 IO45 (I2S_DSDIN) |
| BSP_PIN_PA_EN | 42 | 原理图 | 原理图 IO42 (PA_EN) |
| BSP_PIN_PA_CTRL | 46 | 原理图 | 原理图 IO46 (PA_CTRL) |
| BSP_PIN_BAT_ADC | 4 | 原理图 | 原理图 IO4 (BAT_ADC) |
| BSP_PIN_BAT_CTRL | 17 | 原理图 | 原理图 IO17 (BAT_Control) |
| BSP_PIN_BAT_KEY | 18 | 原理图 | 原理图 IO18 (BAT_KEY) |
| BSP_PIN_BTN_BOOT | 0 | 原理图 | 原理图 IO0 (BOOT0) |

**v0.2.1 的三条「已知缺口」在 v0.3.0 全部由官方 Demo 解决**，且其中一条的结论与提问方式本身相反：

| v0.2.1 的提法 | v0.3.0 的结论 |
|---|---|
| SD_CS 选通引脚未在原理图中明确标记，需进一步查证 | **本板没有 SD_CS。** 官方驱动走 SD_MMC 1-bit (SDIO)，只有 CLK/CMD/D0 三根线，原理图 SD_CS 处的 R41 NC 与此吻合。问题问错了方向 —— 见 §4.5 |
| 触摸 IC 型号与 I2C 地址未明示 | **FT6336 @ 0x38**（§1 #12/#15）。v0.7.7 起 `BSP_HAS_TOUCH=1`，实现见 `touch.cpp` |
| 全刷/局刷 LUT 波形表未知（推测走 OTP 内置） | 官方驱动**自带两张 LUT**：`WF_Full_1IN54[159]` 与 `WF_PARTIAL_1IN54_0[159]`，经 `EPD_SetLut()` 下发，**不是 OTP 内置**（§1 #14）。推测错了 |

**剩余缺口**：全刷/局刷的实际耗时（毫秒）只能实测，见 §2。

## 4. 存疑与冲突

### 4.1 v0.1.0 6 个 EPD 引脚撤回（详见 CHANGELOG v0.2.0）

`bsp_pins.h` v0.1.0 中将 EPD 引脚定义为 BUSY=13, RST=12, DC=11, CS=10, CLK=9, DIN=8。
**经查原理图，此定义完全错误，系将引脚号倒置与位移所致。**
原理图 U8 IO 映射表明确给出：
- IO8: EPD_BUSY
- IO9: EPD_RST
- IO10: EPD_D/C
- IO11: EPD_CS
- IO12: EPD_SCLK
- IO13: EPD_SDI

已在 v0.2.0 中更正为原理图确切值。

### 4.2 BUSY 信号极性确认

根据 `1.54inch_e-paper_V2_Datasheet.pdf` Note 1.5-4 规定："When Busy is High the data will not be issued to the module... Busy pad will output high during operation."
**确定 BUSY 为高有效（High = Working/Busy, Low = Idle/Ready）。** 之前软件中若等待 Low 有效将导致永久死等。

> v0.3.0 补：官方驱动 `read_busy()` 与之独立吻合 —— `while(gpio_get_level(busy) == 1) vTaskDelay(5);`，
> 行内注释 `//LOW: idle, HIGH: busy`。两个独立来源指向同一结论，
> 但按 SOURCE_HARVESTING §1，**互证不升级为 `[OK]`，只有实机可以**，标记仍是 `[REF]`。

### 4.3 局刷区域 X 轴 8 像素对齐约束

根据 Datasheet `Set RAM X-address counter (0x44)` 寄存器说明：
X 起始/结束地址 `XSA[5:0]` / `XEA[5:0]` 独立以 1 字节（8 像素）为单位。
**确定局刷区域在 X 轴方向必须按 8 像素对齐。**

> v0.3.0 补：官方驱动两处独立印证 —— `EPD_SetWindows()` 下发 0x44 时写的是 `(Xstart>>3) & 0xFF`；
> 像素寻址 `index = y * 25 + (x >> 3)`，源码注释直接写着 `//25是200/8`。
> 这条是架构前提而不只是参数：`bsp_ui_status()` 的行定位抽象成不成立取决于它
> （SOURCE_HARVESTING §7.3）。标记同上仍为 `[REF]`。

### 4.4 SD 卡 CS 信号存疑 —— **v0.3.0 已解决，且问法本身是错的**

原文：「原理图 SD 模块电路中，SD_CS 标有 R41 NC…在 U8 IO 映射表中未单独列出 `SD_CS` 的 GPIO。
因此 `bsp_pins.h` 中暂未强制定义 `BSP_PIN_SD_CS`，记入已知缺口。」

不定义 `BSP_PIN_SD_CS` 这个处置是对的，但理由错了 —— 不是「还没查到 CS 接在哪」，
而是**这条总线上根本没有 CS**。官方驱动走 SD_MMC 1-bit (SDIO)，只有 CLK/CMD/D0。
R41 NC 不是待解的线索，它本身就是答案。详见 §4.5。

留着这一条不删，是因为「问错了方向」这件事本身值得留档：
缺口清单上写着的问题，可能连问题都不成立。

### 4.5 SD 接口形态：原理图的命名与官方驱动的用法不一致（v0.3.0）

原理图 U8 IO 映射表把 IO40 / IO41 标成 **SD_MISO / SD_MOSI** —— SPI 味的命名。
但官方 Demo `02_Example/Arduino/04_SD_Card/sdcard_bsp.cpp` 用的是：

```c
#define SDMMC_D0_PIN    GPIO_NUM_40
#define SDMMC_CLK_PIN   GPIO_NUM_39
#define SDMMC_CMD_PIN   GPIO_NUM_41
...
sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;           //1线
```

即 **SD_MMC 1-bit (SDIO)**，三根线是 CLK / CMD / D0。

**结论：本板没有 SD_CS。** v0.2.1 把「SD_CS 归属未明」记成已知缺口，
问的方向就是错的 —— 不是「CS 接在哪个 GPIO」，是「这条总线上根本没有 CS」。
原理图 SD_CS 处标注的 R41 NC 与此完全吻合，那个 NC 本身就是答案。

这与 SOURCE_HARVESTING §4.3（名字对不上物理归属）是同一类，
也与 `lcd_085` 栽过的那次同源：那次是官方文档把 SDIO 写成 SPI，
照着写怎么都初始化不了。**信号名里的 MISO/MOSI 不能推出总线形态。**

处理：`BSP_PIN_SD_MISO` / `BSP_PIN_SD_MOSI` 撤回，改为 `BSP_PIN_SD_D0` / `BSP_PIN_SD_CMD`，
并新增 `BSP_SD_BUS_WIDTH=1`。GPIO 号一个都没变，变的是名字与语义。

### 4.6 官方驱动里 SPI 频率的注释与代码不符（v0.3.0）

`epaper_driver_bsp.cpp`：

```c
devcfg.clock_speed_hz = 40 * 1000 * 1000;  //Clock out at 10 MHz
```

代码是 40 MHz，同一行的注释写 10 MHz。**取代码值**（40 MHz）——
注释不会被编译，跑在板子上的是 40 MHz。已如实记入 `BSP_EPD_SPI_FREQ`
并在行内注明这处冲突，避免下一个人看到注释后以为宏写错了。

无实物，这个值未经实测确认；墨水屏 SPI 跑超的症状是刷不出或花屏，无编译期信号。


## 5. 本板与 0.85 的结构性差异

- 墨水屏无实时刷新与背光：`BSP_UI_REALTIME=0`、`BSP_HAS_BACKLIGHT=0`。
- 所有 UI 操作在缓冲区累积，`bsp_ui_flush()` 是唯一物理刷屏出口。
- 挂载 I2C 触摸屏与 RTC(PCF85063)、温湿度(SHTC3)；支持 I2S 音频 (ES8311 Codec + PA)。
