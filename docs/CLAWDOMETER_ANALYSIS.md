# Clawdometer / Clawdmeter 生态分析报告

> 调研日期：2026-08-28
> 触发：调研开源社区里"实时查看 Claude Code 使用额度"的 ESP32 项目（用户记忆中的 "esp claw"），
> 评估与本项目板子（Waveshare ESP32-S3-Touch-ePaper-1.54）的契合度。

---

## 0. 结论先行

- 你记忆里的 "esp claw" ≈ **Clawdmeter**（Hermann Björgvin 发起）及其社区衍生项目。整个小生态叫法混乱：
  Clawdmeter / Clawdometer / ClaudeGauge / TokenMeter，核心都是"**主机跑守护进程算额度 → BLE/WiFi 推给一块 ESP32 → 小屏显示 5 小时窗口和周窗口的用量百分比 + 重置倒计时**"。
- 其中 **`nsyll/clawdometer-eink` 跑在和本项目一模一样的板子上**：Waveshare ESP32-S3-ePaper-1.54，200×200，SSD1681，N8R8，NimBLE，PlatformIO + Arduino。硬件层面 100% 对口，是唯一值得深入看的一个。
- 但它**只跑 macOS 守护进程**，且 e-paper 渲染用的是 **GxEPD2**——和本项目"两条 SSD1681 驱动、`epd_factory.cpp` 逐字节抄参考驱动、自己管 partial refresh"的架构冲突。直接 fork 会丢掉本项目在刷新纪律上踩过的所有坑。
- **推荐路线：不 fork，借鉴其"数据管道 + BLE 载荷设计"，在本项目已有的 `ecosystem_ble` 协议上加一个 `usage.*` 消息和一个"额度页"。** 详见 §5。

---

## 1. "esp claw" 到底是什么

社区里没有叫 "esp claw" 的项目，你记的是 **Clawd**（Anthropic 的爪子吉祥物）+ **meter/ometer**。时间线大致是 2026-05 一批：

| 项目 | 作者 / 仓库 | 屏 | 传输 | 主机端 | 许可 |
|---|---|---|---|---|---|
| **Clawdmeter**（原版） | `HermannBjorgvin/Clawdmeter`（`maxeem/` 是同名 fork） | 多种 Waveshare AMOLED/LCD 触摸屏（2.16" / 1.8" / **1.54" LCD** 等） | BLE（自定义 GATT + 标准 HID 键盘） | Python 守护进程，macOS / Linux / Windows 都有 | 未正式开源（含专有字体和 Anthropic 版权吉祥物素材） |
| **clawdometer-eink** | `nsyll/clawdometer-eink` | **Waveshare ESP32-S3-ePaper-1.54，200×200 e-ink（= 本项目板子）** | BLE（自定义 GATT） | 单文件 Python，**仅 macOS** | **MIT** |
| **ClaudeGauge** | `dorofino`（Hackster） | ESP32-S3 + 小 TFT，LCARS 风格 UI | — | — | 开源 |
| **TokenMeter** | `alestanalves/TokenMeter` | ESP32-2432S028R（CYD），2.8" ILI9341 TFT + XPT2046 触摸 | **WiFi**（连主机 HTTP 桥，端口 8787） | Python HTTP server，读 `~/.claude/projects` + `~/.codex/state_5.sqlite` | 未注明 |
| Claude Desktop Buddy | Anthropic 官方 | ESP32-S3 桌面伴侣 | BLE | 官方 | 官方开源 |

对本项目**只有 `clawdometer-eink` 硬件对口**，下面重点分析它。

---

## 2. `clawdometer-eink` 深度分析

### 2.1 硬件（与本项目完全一致）

- Waveshare ESP32-S3-ePaper-1.54，ESP32-S3 **N8R8**（8MB flash + 8MB PSRAM）
- 1.54" 单色 e-ink，200×200，**SSD1681** 控制器
- ES8311 codec + 喇叭（用于"轮到你了"的提示音）
- 两个按键（BOOT / PWR）
- 板载电池充电/自锁电路

这正是本项目 `boards/epaper_154/bsp_pins.h` 描述的板子。无需改任何硬件抽象。

### 2.2 固件栈（与本项目部分冲突）

| 维度 | clawdometer-eink | 本项目 | 冲突程度 |
|---|---|---|---|
| 构建 | PlatformIO + Arduino | PlatformIO + Arduino | ✅ 一致 |
| BLE | NimBLE-Arduino | NimBLE-Arduino @ 2.3.6 | ✅ 一致 |
| JSON | ArduinoJson | ArduinoJson @ 7.4.2 | ✅ 一致 |
| **EPD 渲染** | **GxEPD2**（库自己管 partial/full） | **两条自研 SSD1681 驱动**，`epd_factory.cpp` 逐字节抄官方参考，partial refresh 是 session-based、手动管 RAM `0x26` 基准图 | ⚠️ **强冲突** |
| 音频 | ES8311 I2S driver | 本项目当前 app 不驱动音频 | 中等（可选功能） |
| 电源 | 深睡 ~20µA，30 分钟 BLE peek；<3% 自动关机 | 当前 app 无深睡逻辑 | 中等 |

**关键点：** 本项目 CLAUDE.md 明确写了"所有真实刷新走 `epd_factory.cpp`，不要把参考命令序列改写进自研驱动"、"partial refresh 的 BUSY 时长是验收标准（~300–500ms，不是看图对不对）"。GxEPD2 是另一套 partial refresh 实现，引入它等于在这块板子上开第三条 SSD1681 驱动路径，和现有纪律正面撞车。

### 2.3 BLE 协议（可借鉴的核心资产）

自定义 GATT，设备名 `Clawdometer`：

| 组件 | UUID |
|---|---|
| Service | `4c41555a-4465-7669-6365-000000000001` |
| RX（守护进程写） | `4c41555a-4465-7669-6365-000000000002` |
| REQ（设备 notify 请求刷新） | `4c41555a-4465-7669-6365-000000000004` |

- MTU 请求 512，载荷 ~450B 单次 write-without-response 送完
- 守护进程每 **60s** 推一个 JSON

JSON 载荷字段：

| 字段 | 类型 | 含义 |
|---|---|---|
| `s` | int | 会话（5 小时窗口）用量 % |
| `sr` | int | 距 5 小时重置的分钟数 |
| `w` | int | 周（7 天）用量 % |
| `wr` | int | 距 7 天重置的分钟数 |
| `st` | enum | 限流状态：`allowed` / `allowed_warning` / `rejected` / `limited` |
| `acct` | enum | 账户档：`pro` / `ent` |
| `ok` | bool | 健康标志 |
| `cc` | enum | Claude Code 状态：`working` / `needs-you` / `question` / `done` |
| `ccm` | string | 状态文字（≤80 字符） |
| `cct` | epoch | 状态时间戳 |
| `hh` | int[24] | 每小时 token（千） |
| `h` | int[30] | 每日 token（千） |
| `d` / `d7` / `d30` | object | token 数、工作分钟、用户轮次分钟、会话数、工具调用数 |

**刷新触发：** 设备只在 `s` / `w` / `st` / Claude Code 状态变化时重绘，其余数据静默更新——刻意减少 e-paper 闪烁。这个"数据到了不一定刷屏"的设计和本项目 `APP_PARTIAL_MAX_STREAK=4`（连续 partial 若干次后强制 full）的思路是一致的。

### 2.4 数据从哪来（最有价值的部分）

守护进程（单文件 Python，依赖 `bleak` + `httpx`）：

1. 从 **macOS Keychain 读 Claude Code 的 OAuth token**（复用已有登录，零配置）
2. 用该 token 向 Anthropic API 发最小请求，从 **响应头里的 rate-limit 字段**拿 5h/7d 用量百分比和重置时间
3. 读 `~/.clawd/cc_status.json`（Claude Code hook 实时写的状态）
4. 读 `~/.claude/projects/**/*.jsonl`（Claude Code 本地会话日志，可完整回填历史）
5. 本地算好所有统计，拼 JSON，每 60s BLE 推给设备
6. 终端里还顺带画一个 dashboard

**全部本地计算，不上云。** hook 是可选的（没 hook 也能显示额度，只是没有状态横幅和提示音）。

### 2.5 已知限制

- 守护进程**只有 macOS 版**（Clawdmeter 原版才有 Linux/Windows）
- 必须从有蓝牙权限的终端启动（IDE 内置终端不行）
- 刷固件会重启设备，守护进程要重连
- 状态横幅需要重载 hook（装完 hook 要重启 Claude Code 会话）

---

## 3. 这个想法对本项目意味着什么

本项目当前是"家庭视频相框"：开机 full-refresh 第一帧，右侧点屏 partial refresh 下一帧，左侧上一帧。最近一次提交（`76b431a`）刚把 **`esp32_test` 的 NimBLE 生态协议**移植进来，让 BLE 的左/右键能触发和触摸一样的翻帧。

也就是说：**BLE 通道、NimBLE、ArduinoJson、partial refresh 纪律——本项目已经全有了。** 缺的只是：

1. 一个主机端脚本，把额度数据推过来；
2. 固件里一个"额度页"，用现有 `epd_factory.cpp` 路径画进度条；
3. 生态协议里加一个 `usage.push` 之类的消息类型。

本项目的 `ecosystem_protocol.h` 已经定义了一套自己的 GATT（`7e0d0001-…` 系列）、消息上限 256B、`OP_UI_MESSAGE` / `OP_STATE_GET` 等操作码，以及能力位（`input.remote.key` 等）。加额度功能就是**再加一个 `OP_USAGE_PUSH` + 一个 `usage` 能力位**，走已有的 COMMAND/RESPONSE/STATE 特征值，不需要引入 Clawdometer 的 GATT。

⚠️ 注意 256B 上限：Clawdometer 的完整载荷 ~450B（带 `hh[24]` / `h[30]` 直方图）。若走本项目现有协议，第一版应砍掉直方图，只推 `s/sr/w/wr/st/cc/ccm` 这几个标量，约 60–80B，一条消息够。直方图以后要么提高 MTU，要么分片。

---

## 4. 三条可选路线

### 路线 A：直接 fork `clawdometer-eink`
- **工作量**：小（硬件对口，能直接刷）
- **代价**：丢掉本项目的相框功能；引入 GxEPD2 = 这块板上第三条 SSD1681 驱动，违反 CLAUDE.md 的刷新纪律；只有 macOS 守护进程
- **结论**：只适合"我想要个额度表、不在乎相框"。**不推荐**在本仓库里做。

### 路线 B：借数据管道，自写主机脚本 + 固件额度页（推荐）
- 主机端：照 §2.4 写一个跨平台 Python 脚本（你在 Windows，正好参考 Clawdmeter 原版的 Windows 实现读 `%USERPROFILE%\.claude\.credentials.json`，而不是 clawdometer-eink 的 Keychain 版）
- 固件端：
  - `ecosystem_protocol.h` 加 `OP_USAGE_PUSH` + `CAP_USAGE`
  - `ecosystem_ble.cpp` 的命令分发里加一个 handler，存进 `app_state`
  - `src/` 加一个额度页：两条 partial-refresh 进度条（5h / 7d）+ 重置倒计时，复用 `bsp_ui_*` 和 `epd_factory.cpp`
  - 用一个按键或触摸手势在"相框 / 额度页"之间切
- **工作量**：中。BLE、JSON、partial refresh 都是现成的；主要是主机脚本 + 一个新页面的排版
- **代价**：主机脚本要自己维护；Anthropic rate-limit 响应头的字段名没有官方文档，得抓包确认（Clawdmeter/clawdometer-eink 的源码就是现成样例）
- **结论**：**推荐。** 最符合本仓库架构和"文档驱动、留注释解释 why"的风格。

### 路线 C：只做主机端终端 dashboard，不碰固件
- 如果只是想"看额度"，`ccusage`、`Claude-Code-Usage-Monitor` 这类纯 CLI 工具已经够用，不需要硬件
- **结论**：如果硬件显示不是刚需，这是零成本方案；但就没有"桌面小物件"的乐趣了

---

## 5. 风险与注意事项

| 风险 | 说明 | 缓解 |
|---|---|---|
| Anthropic rate-limit 头非公开契约 | 字段名/语义可能随时变 | 抄 clawdometer-eink 守护进程源码里的解析逻辑；脚本里对缺字段做容错 |
| 载荷超 256B | 本项目协议上限；带直方图会炸 | 第一版只推标量；直方图后置 |
| 引入 GxEPD2 | 与 `epd_factory.cpp` 冲突，违反 CLAUDE.md | 路线 B 完全不用 GxEPD2，用现有驱动画进度条 |
| partial refresh 基准图失同步 | 额度页和相框切换时若不做 `partial_end → fb_flush_full → partial_begin` 会残影 | 遵守 CLAUDE.md "Partial refresh is session-based" 那一节的 invariant |
| OAuth token 安全 | 主机脚本要读凭据文件 | 脚本只在本地用，不外发；不要把 token 写进日志 |
| macOS-only 守护进程 | clawdometer-eink 不能直接在 Windows 跑 | 参考 Clawdmeter 原版 Windows 版，或自己用 `keyring` / 直接读 `.credentials.json` |
| 版权素材 | Clawdmeter 含 Anthropic 吉祥物和专有字体，未开源 | 用 `clawdometer-eink`（MIT）作参考；不要拷贝吉祥物位图 |

---

## 6. 建议的下一步

1. 先跑**路线 C** 的一个 CLI 工具（`npx ccusage` 或 `Claude-Code-Usage-Monitor`），确认额度数据长什么样、够不够用。
2. 读 `nsyll/clawdometer-eink` 的守护进程源码（单文件 Python），把"读凭据 → 请求 → 解析响应头"这段逻辑抄成一个 Windows 能跑的脚本，先只打印 JSON。
3. 在 `ecosystem_protocol.h` 加 `OP_USAGE_PUSH` + `CAP_USAGE`，`ecosystem_ble.cpp` 加 handler 存进 `app_state`，串口打印收到的值。
4. 最后才做额度页 UI，用 M1（`APP_MODE=2`）的 partial-refresh 移动方块代码作起点画进度条。
5. 按 `boards/epaper_154/CHANGELOG.md` 的格式记一条，Validation 段写清楚"BLE 载荷回归 + 主机脚本在 Windows 实测"。

---

## 7. 参考链接

- Clawdometer E-Ink（同款板子，MIT）: https://github.com/nsyll/clawdometer-eink
- Clawdmeter 原版（含 Windows 守护进程）: https://github.com/HermannBjorgvin/Clawdmeter
- Clawdmeter fork: https://github.com/maxeem/clawdmeter
- TokenMeter（WiFi + CYD，读本地日志的思路可参考）: https://github.com/alestanalves/TokenMeter
- Adafruit 博客介绍: https://blog.adafruit.com/2026/05/12/making-a-claude-usage-display-with-clawdmeter/
- CNX Software 介绍: https://www.cnx-software.com/2026/05/14/clawdmeter-a-diy-esp32-s3-desk-dashboard-for-claude-code-token-usage-monitoring/
- Hackster（ClaudeGauge）: https://www.hackster.io/dorofino/claudegauge-real-time-ai-usage-monitor-on-esp32-s3-with-a-a82d4b
