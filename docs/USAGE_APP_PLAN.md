# 额度页 App 落地计划（Clawdometer 路线 B）

> 状态：已落地（固件、主机脚本和文档实现已进入当前工作区；硬件实测仍按 Validation 清单执行）。

> 制定日期：2026-08-29
> 上游：`docs/CLAWDOMETER_ANALYSIS.md` §4 路线 B、§6 建议下一步；`docs/APP_SHELL_PLAN.md` §里程碑 M-F；
> **取数方式实测见 `docs/CLAUDE_CODEX_USAGE_INVESTIGATION.md`（2026-08-29，本机 Claude Code 2.1.250 / Codex 0.150.1）**。
> 目标：把"实时查看 Claude Code 使用额度"做成 app shell 里的**一个 `app_entry_t`**，
> 复用现有 `ecosystem_ble` GATT、`epd_factory.cpp` partial refresh 路径和 `src/shell/` 交接逻辑，
> **不引入 Clawdometer 的 GATT，不引入 GxEPD2，不 fork `clawdometer-eink`**。

App shell（launcher + 注册表 + 状态栏 + 设置）已在 v0.9.0 落地。本计划只新增第 3 个 app
（`src/apps/usage/`）、一条 BLE 消息、一个能力位，和一个 Windows 主机脚本。

---

## 0. 结论先行

- 固件端工作量集中在**一个新 app + 一个 BLE handler**。BLE / NimBLE / ArduinoJson / partial refresh
  纪律 / app 生命周期都是现成的。
- **同时监控 Claude + Codex**：每条 `usage.push` 带一个 `p` 字段（`"claude"`/`"codex"`），
  model 按 provider 存成定长数组，额度页把两家画在同一屏。Codex 的 5h / 周窗口和 Claude 同构，
  schema 复用，见 §4.5。
- **低频推送：默认 4 次/天（每 ~6h）+ 每次 BLE (重)连上立即补一次**。用户决定：额度不需要秒级新鲜。
  代价是 5h 会话窗在两次推送间必然过期——设备端本地递减 `sr`，`sr` 归零后 session 条画斜纹标 `~`
  （§5.3）。7 天窗 6h 粒度足够。对 e-paper 是净好处：partial 次数少一个数量级。
- 第一版**只推标量**（`p/s/sr/w/wr/st/cc/ccm/acct/ok`），单条约 130–160B，远低于
  `ecp::MAX_MESSAGE_BYTES=256`。直方图（`hh[24]`/`h[30]`）后置，见 §4.4。
- 主机脚本 **Windows-first**，**不读凭据文件推断额度**（`.credentials.json`/`auth.json`/`state_5.sqlite`
  都只有认证信息，没有实时用量）。取数走各家 CLI 已有的通道（调查报告 §5）：
  - **Claude**：`claude -p "/usage" --output-format json --no-session-persistence`，正则解析 `result` 里的
    `NN% used · resets <日期>` 文本；需要结构化字段时回退逆向接口 `GET /api/oauth/usage`（OAuth Bearer）。
    实测该调用 `num_turns:0 / total_cost_usd:0`——**不跑模型任务、不计费**。
  - **Codex**：`codex app-server --listen stdio://` 的 JSON-RPC `account/rateLimits/read` 拿快照，
    常驻监听 `account/rateLimits/updated`；离线回退扫最近的 `rollout-*.jsonl` 找最后一个 `payload.rate_limits`。
  - 两家都是 primary(5h) / secondary(7d) 的 `used_percent + resets_at` 结构，直接映射到 `s/sr/w/wr`。
- 渲染：状态栏下方两条水平进度条（5h / 7d）+ 重置倒计时 + 限流横幅 + Claude Code 状态行。
  **数据到了不一定刷屏**——只在 `s`/`w`/`st`/`cc` 变化时重绘，和 `APP_PARTIAL_MAX_STREAK` 思路一致。

---

## 1. 已确定的架构决定

| 项 | 决定 | 理由 |
|---|---|---|
| App 形态 | `src/apps/usage/`，非全屏（`fullscreen=false`），内容区在状态栏下方 y≥26 | 额度信息量小，状态栏的 BLE 链路图标正好指示主机脚本是否在线 |
| 传输 | 现有 `ecosystem_ble` GATT（`7e0d0001-…`），新增 `OP_USAGE_PUSH` + `CAP_USAGE` | 分析 §3：不需要 Clawdometer 的 `4c41…` GATT |
| 多 provider | 每条 push 带 `p`（`claude`/`codex`），model 定长数组 `USAGE_PROVIDER_MAX=3`，额度页同屏画两家；每 provider 独立 stale 判定 | 用户同时用 Claude 和 Codex；两家窗口同构，一套 schema 够用 |
| 取数通道 | Claude：`claude -p "/usage" --output-format json`（回退 `GET /api/oauth/usage`）。Codex：`codex app-server` 的 `account/rateLimits/read` + `.../updated`（回退扫 `rollout-*.jsonl`）。**不碰凭据文件** | 调查报告 §1/§5：两者都没有可靠的本地限流状态文件；CLI 通道不计费、字段稳定度更高 |
| 载荷 | 单条仅标量，≤160B；`ccm` 在 handler 里截断到 64 字节 | 256B 上限；直方图后置 |
| 推送节奏 | 单向：默认 **4 次/天（`--interval` 默认 21600s，脚本启动起算的相对间隔，不用 cron/固定钟点）** + 每次 BLE (重)连上补一次；Codex 可在 `rateLimits/updated` 触发时机会性多推（限流 ≥30min）。设备不主动请求 | 用户决定：额度不需高频，相对间隔实现最简。低频 = e-paper 省刷新、省电；on-connect 补推保证刷完固件即有新数 |
| 会话窗过期 | 5h 窗在两次推送间会过期；设备本地按分钟递减 `sr`，归零后 session 条画斜纹 + `~`，不显示旧 % | 6h 间隔下无法避免；7d 窗不受影响 |
| 分层 | `ecosystem_ble.cpp` 只加一个 handler + 一个 `__attribute__((weak))` 钩子；解析后的数据交给 app 层 | 与 `bsp_remote_key_event` / `bsp_app_state_json` 同构，保持 BLE 层板级无关 |
| 数据落点 | `src/apps/usage/usage_model.{h,cpp}`：最近一次快照 + 接收时刻 + 序号。始终编译（不随 app 激活） | 设置页要显示"最近一次 push 时间"，状态栏可显示 stale |
| 刷新 | 每次变化一次 partial refresh；连续 `USAGE_PARTIAL_MAX_STREAK` 次后经 `shell_full_refresh_current()` 强制 full | 抄 `app_family_video.cpp` 的 streak 逻辑，防 e-paper 残影累积 |
| 倒计时 | `sr`/`wr` 是 push 时刻的"距重置分钟数"；本地按 `rx_millis` 递减，显示的分钟值变化时才重绘 | 不靠主机每分钟推一次；省刷新 |
| 音频提示音 | **不做**。板子有 ES8311，但当前 shell 不驱动音频 | 分析 §2.2：中等优先级可选功能，留作增量 |
| 深睡 / 自动关机 | **不做** | 当前 shell 无深睡逻辑；不在本计划范围 |
| 素材 | 图标为**原创**条形/仪表盘字形；不含 Anthropic 吉祥物 | 分析 §5 版权风险 |

---

## 2. 模块布局

```
src/apps/usage/
  app_usage.h  app_usage.cpp     # app_entry_t 的 4 个回调；渲染 + 输入
  usage_model.h  usage_model.cpp # usage_snapshot_t + 最近快照存储 + dirty 序号
                                 # 提供强符号 bsp_usage_push_apply()，覆盖 ble 层的 weak 桩
assets/generated/
  icons.c  icons.h               # 新增 SHELL_ICON_USAGE[SHELL_ICON_LEN]
lib/ecosystem_protocol/
  ecosystem_protocol.h           # 加 OP_USAGE_PUSH、CAP_USAGE
lib/ecosystem_ble/
  ecosystem_ble.cpp              # 加 handle_usage_push + weak bsp_usage_push_apply + COMMANDS 表一行 + add_capabilities 一行
src/shell/
  app_registry.cpp               # 加一行注册
  status_bar.cpp                 # 可选：链路符号旁加 stale 标记（增量）
src/apps/settings/
  app_settings.cpp               # "USAGE PUSH" 行：最近一次 push 的 age + st（增量）
tools/
  usage_push.py                  # Windows-first 主机脚本
boards/epaper_154/
  bsp_caps.h                     # 加 BSP_CAP_USAGE 1
  CHANGELOG.md                   # 一条 [0.10.0]，带 Validation
docs/
  USAGE_APP_PLAN.md              # 本文件
  EPAPER_154_APP_GUIDE.md        # 补 usage.push 协议表 + 主机脚本运行方式
```

`lib/` 不新增大件。`assets/generated/` 增约 800B（一个 80×80 1bpp 图标）。

---

## 3. 数据模型 `src/apps/usage/usage_model.h`

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define USAGE_PROVIDER_MAX 3   /* claude, codex, + 1 预留 */

typedef struct usage_snapshot {
    char     provider[12];       /* p   "claude" / "codex" */
    uint8_t  session_pct;        /* s   会话(5h)用量%，clamp 0..100 */
    uint16_t session_reset_min;  /* sr  距 5h 重置的分钟数 */
    uint8_t  week_pct;           /* w   周(7d)用量% */
    uint16_t week_reset_min;     /* wr  距 7d 重置的分钟数 */
    char     limit_state[16];    /* st  allowed / allowed_warning / rejected / limited */
    char     acct[8];            /* acct pro / ent（Codex 可空） */
    bool     ok;                 /* ok  健康标志 */
    char     cc_state[16];       /* cc  working / needs-you / question / done（仅 Claude hook 提供） */
    char     cc_msg[64];         /* ccm 已截断到 63 字节 + NUL */
    uint32_t rx_millis;          /* 本地接收时刻（millis），用于倒计时递减与 stale 判定 */
    uint32_t seq;                /* 每次成功 apply 自增（全局，跨 provider）；app 检测"有新数据" */
} usage_snapshot_t;

/* 由 ecosystem_ble handler 调用（BLE 已在主 loop 上下文分发，非中断）。
 * 按 in->provider 命中/新建槽位；字段范围收敛 + ccm 截断；写入并自增全局 seq。
 * provider 表满且无匹配 → 丢弃并 WARN。 */
void usage_model_apply(const usage_snapshot_t* in);

/* 按 provider 名读取；未收到过该 provider → 返回 NULL。单线程读取安全。 */
const usage_snapshot_t* usage_model_get(const char* provider);

/* 按槽位遍历（0..count-1），给渲染循环用。 */
int  usage_model_count(void);
const usage_snapshot_t* usage_model_at(int slot);

/* 指定 provider 距今毫秒；未收到过返回 UINT32_MAX。 */
uint32_t usage_model_age_ms(const char* provider);

/* 全局 seq；== 0 表示还没收到过任何 push。 */
uint32_t usage_model_seq(void);
```

槽位一旦为某 provider 建立就不回收——Codex 脚本停了，该槽保留最后一次数据并转 stale，不消失。

---

## 4. BLE 协议扩展

### 4.1 `lib/ecosystem_protocol/ecosystem_protocol.h`

```c
constexpr const char* OP_USAGE_PUSH = "usage.push";
constexpr const char* CAP_USAGE     = "usage";
```

放在 v0.4 扩展块之后，注释标 `// usage app extension (board contract 0.10.0)`。

### 4.2 载荷 schema（命令信封内，全部是 `op` 的同级字段）

| 字段 | 类型 | 必填 | 含义 | 缺省/容错 |
|---|---|---|---|---|
| `p`  | string | 否 | provider：`claude` / `codex` | 缺 → `"claude"`（向后兼容单 provider） |
| `s`  | int 0..100 | 是 | 会话用量% | 缺 → 拒绝（`BAD_REQUEST`） |
| `sr` | int ≥0 | 是 | 距 5h 重置分钟 | 缺 → 0，显示 `--` |
| `w`  | int 0..100 | 是 | 周用量% | 同 `s` |
| `wr` | int ≥0 | 是 | 距 7d 重置分钟 | 缺 → 0 |
| `st` | string | 否 | 限流状态 | 缺/未知 → `"allowed"` |
| `acct` | string | 否 | 账户档 | 缺 → `""`（不显示） |
| `ok` | bool | 否 | 健康标志 | 缺 → true |
| `cc` | string | 否 | Claude Code 状态 | 缺 → `""`（不显示状态行） |
| `ccm`| string ≤80 | 否 | 状态文字 | handler 截断到 63 字节 |

样例（一次 write-without-response）：

```json
{"v":1,"rid":41,"op":"usage.push","p":"claude","s":42,"sr":167,"w":73,"wr":5040,
 "st":"allowed_warning","acct":"pro","ok":true,"cc":"working","ccm":"Editing app_usage.cpp"}
{"v":1,"rid":42,"op":"usage.push","p":"codex","s":55,"sr":70,"w":31,"wr":5890,
 "st":"allowed","ok":true}
```

单条序列化后约 150B（`ccm` 20 字节时）；`ccm` 顶到 63 字节时约 195B。< 256，每条一次 write。

### 4.3 handler（`ecosystem_ble.cpp`）

```c
__attribute__((weak)) void usage_model_apply(const usage_snapshot_t*) {}  /* 见下方说明 */

void handle_usage_push(uint32_t rid, JsonDocument& req)
{
#if defined(BSP_CAP_USAGE) && BSP_CAP_USAGE
    if (!req["s"].is<int>() || !req["w"].is<int>()) {
        notify_response(rid, false, ecp::CODE_BAD_REQUEST, "s and w required");
        return;
    }
    usage_snapshot_t snap = {};
    snprintf(snap.provider, sizeof(snap.provider), "%s", req["p"] | "claude");
    snap.session_pct       = (uint8_t)constrain((int)req["s"], 0, 100);
    snap.week_pct          = (uint8_t)constrain((int)req["w"], 0, 100);
    snap.session_reset_min = (uint16_t)max(0, (int)(req["sr"] | 0));
    snap.week_reset_min    = (uint16_t)max(0, (int)(req["wr"] | 0));
    snprintf(snap.limit_state, sizeof(snap.limit_state), "%s", req["st"] | "allowed");
    snprintf(snap.acct,        sizeof(snap.acct),        "%s", req["acct"] | "");
    snap.ok = req["ok"] | true;
    snprintf(snap.cc_state,    sizeof(snap.cc_state),    "%s", req["cc"] | "");
    snprintf(snap.cc_msg,      sizeof(snap.cc_msg),      "%s", req["ccm"] | "");  /* snprintf 截断 */
    snap.rx_millis = millis();
    usage_model_apply(&snap);
    LOG_I("ECP", "usage.push p=%s s=%u sr=%u w=%u wr=%u st=%s cc=%s",
          snap.provider, snap.session_pct, snap.session_reset_min, snap.week_pct,
          snap.week_reset_min, snap.limit_state, snap.cc_state);
    notify_empty_ok(rid);
#else
    notify_response(rid, false, ecp::CODE_UNSUPPORTED, ecp::OP_USAGE_PUSH);
#endif
}
```

- `COMMANDS[]` 加 `{ecp::OP_USAGE_PUSH, handle_usage_push}`。
- `add_capabilities()` 加 `#if defined(BSP_CAP_USAGE) && BSP_CAP_USAGE  caps.add(ecp::CAP_USAGE);`。

**分层关键**：`usage_snapshot_t` 的定义必须让 `ecosystem_ble.cpp` 也能 include。两条路线：

- **推荐**：把 `usage_snapshot_t` 结构体单独放 `lib/ecosystem_protocol/usage_types.h`（纯 POD，无依赖），
  `ecosystem_ble.cpp` 和 app 都 include 它。`usage_model_apply` 在 ble 层是 `weak` 空桩，
  `usage_model.cpp` 提供强符号——和 `bsp_remote_key_event` / `bsp_app_state_json` 完全同构。
- 备选：handler 把 usage 字段重新 `serializeJson` 成一个小字符串，weak 钩子签名
  `void bsp_usage_push_json(const char*)`，app 侧自己再 parse 一次。免共享结构体，代价是双解析
  （~1ms，可忽略）。若不想动 `lib/ecosystem_protocol/` 就用这个。

### 4.4 直方图后置

`hh[24]` + `h[30]` 约 +250B，单条必爆 256。三条出路，都不在 v1：
1. 提高 MTU（`NimBLEDevice::setMTU` 已是 259；要到 ~520 并让主机协商）。
2. 分片：`usage.hist` 带 `part`/`total`，app 侧重组。
3. 直接不做——200×200 上画 24 根柱子价值有限。

### 4.5 多 provider（Claude + Codex）

- **一条 push = 一个 provider**。主机脚本每个推送周期（默认 6h）循环推 `p:"claude"`、`p:"codex"`
  两条，`rid` 各自自增。分两条而不是一条塞两家：单条小、易扩展第三家、某一家数据拿不到时
  只是不推那条（对应槽位转 stale），不影响另一家。
- Codex 的额度窗口与 Claude 同构（5 小时滚动 + 周窗口），`s/sr/w/wr` 语义直接复用。
  Codex 无 `acct`、无 `cc`/`ccm`（那是 Claude Code hook 特有）——缺省留空，渲染时跳过。
- `st` 枚举对 Codex 可能取值不同（如它自己的 `limited`）；handler 不校验枚举，原样存，
  渲染层对未知值兜底（§5.2）。
- 未知 `p`（既非 `claude` 也非 `codex`）：model 照样建槽（`USAGE_PROVIDER_MAX` 有 1 个预留位），
  渲染循环按 `usage_model_at()` 遍历，标题直接用 `provider` 字段大写——天然支持将来第三家。
- 向后兼容：不带 `p` 的旧 push 当 `claude` 处理，单 provider 部署无需改脚本。

---

## 5. 额度页渲染 `src/apps/usage/app_usage.cpp`

### 5.1 布局（内容区 y 27..199，宽 200，字体 6px/字，参考 `app_settings.cpp`）

两个 provider 同屏，各一小节：标题行 + 两条紧凑进度条（5h / 7d），每条把
`pct` 和倒计时压在同一行行尾。`usage_model_count()` 循环渲染，坐标按 slot 偏移。

```
y=28   CLAUDE                  PRO          slot 标题（provider 大写）+ acct 右对齐
y=40   5H [///////////      ]  due          sr 本地已归零 → 斜纹 + due：窗口可能已重置，等下次推送
y=56   7D [=======          ] 73%  ~2d      label 2字 + bar x=22 w=104 h=12 + pct + 倒计时粗桶
y=72   ----------------------------------   细分隔线
y=76   CODEX                               (无 acct)
y=88   5H [=============    ] 55%  ~1h
y=104  7D [====             ] 31%  >3d
y=120  ==================================  粗分隔线
y=126  LIMITED                             两家里更严重的 st（§5.2）；正常显示 OK
y=142  cc: working                         仅当某家有 cc；Codex 无此行
y=156  Editing app_usage.cpp               ccm，截断到 32 字（6px*32≈192）
y=176  upd 2h ago   codex NO HOST          每 provider 数据龄（小时粒度）；age>USAGE_STALE_MS → NO HOST + 边框
```

倒计时粗桶：5h 窗用 `>3h/~2h/~1h/<30m/<10m/due`；7d 窗用 `>3d/~2d/~1d/<12h/<3h/due`。

3 个 provider 时，行距压到 5h/7d 各一条无标题、靠 label 前缀区分（`C5H`/`X5H`）——
留到真有第三家时再排。

进度条：外框 4 条细 `bsp_ui_fb_fill_rect` 画 1px 边；填充
`bsp_ui_fb_fill_rect(x+2, y+2, (104-4)*pct/100, h-4, true)`。`pct==0` 不填。

### 5.2 限流横幅（`st` → 文案）

| `st` | 横幅文字 | 备注 |
|---|---|---|
| `allowed` | `OK` | `ok==false` 时改画 `HOST ?` |
| `allowed_warning` | `WARNING` | |
| `rejected` | `REJECTED` | |
| `limited` | `LIMITED` | |
| 未知 | 原样打印（截断 12 字） | 容错 Codex 自己的枚举 / 未来新增 |

横幅取**两个 provider 里更严重的那个** `st`（严重度 `limited > rejected > allowed_warning > allowed`），
并在文字前缀标是哪家（`CLAUDE LIMITED` / `CODEX WARNING`）。两家都正常 → `OK`。
不做反色/闪烁（e-paper 上闪烁难看且费刷新）；`limited`/`rejected` 时横幅整行加一个 1px 边框方块以示强调。

### 5.3 刷新纪律

```c
#ifndef USAGE_PARTIAL_MAX_STREAK
#define USAGE_PARTIAL_MAX_STREAK 6
#endif
#define USAGE_STALE_MS     (7u * 3600u * 1000u)   /* 一个 6h 周期没到 = host 可能挂了 */
#define USAGE_CD_POLL_MS   60000u                  /* 每分钟本地重算一次剩余时间 */
```

**倒计时按粗桶显示，不是每分钟跳数字**——低频推送的精神也要贯彻到画面：
剩余时间本地算（`rx_millis + sr*60000 - now`），但显示成
`>3h` / `~2h` / `~1h` / `<30m` / `<10m` / `due`。每 `USAGE_CD_POLL_MS` 重算，
**只有桶标签变了才重画**。一个 5h 窗一天也就几次重画，不是 300 次。

- `on_enter()`：清内容区 → 画全部 provider → `bsp_ui_flush_partial()`（shell 已 `partial_begin`）。
  记 `last_seq = usage_model_seq()`，并对每个 slot 缓存 `{s,w,st,cc, 倒计时分钟值}` 作变化基准。
- `tick()`：`shell_wait_event(30000)`（30s 超时——低频下没必要 2s 轮询；`ecp_ble_loop()` 仍在
  wait 内被泵，push 到了会即时落进 model）。事件返回后：
  - `SHELL_EV_HOME` / `SHELL_EV_BACK` → `shell_show_launcher()`。
  - 其余（含 `SHELL_EV_TIMEOUT`）→ 调 `maybe_redraw()`。
- `maybe_redraw()`：满足**任一**才重画（**任意** provider 命中即可）——
  1. `usage_model_seq() != last_seq` 且某 slot 的 `s`/`w`/`st`/`cc` 相对基准变了（纯 `sr`/`wr` 数值变化不触发）；
  2. 某 slot 某个窗口的**倒计时桶标签**变了（`>3h`→`~2h` 之类；`USAGE_CD_POLL_MS` 检查）；
  3. 某 slot `sr` 本地归零：session 条改画斜纹 + `due`（"窗口可能已重置，等下次推送"），不再显示旧 `s%`；
  4. 某 slot age 越过 `USAGE_STALE_MS`（host 挂了）。
  重画时：`streak < USAGE_PARTIAL_MAX_STREAK` → `bsp_ui_flush_partial(); streak++`；
  否则 → `shell_full_refresh_current(); streak=0`（这是 CLAUDE.md「session-based partial」
  唯一合法的插入点，`app_family_video.cpp` 同款用法）。
  每次都打印 `busy=%lums`（`bsp_epd_last_busy_ms()`）——partial 应 ~300–500ms，full ~1755ms，
  这是 M1 验收口径，不是"看着对"。
- `on_exit()`：无操作（model 常驻，下次 `on_enter` 直接读）。

### 5.4 输入与返回语义

| 输入 | 行为 |
|---|---|
| `SHELL_EV_TAP`（上半屏 / 下半屏） | 切"聚焦视图"：只画被点的那个 provider（大字大条），再点回双栏总览 |
| `SHELL_EV_TAP` 长按 | 强制一次 full refresh（清残影） |
| `SHELL_EV_KEY` `left`/`right` | 在 总览 → claude 聚焦 → codex 聚焦 之间循环 |
| `SHELL_EV_KEY` `enter` | 强制立即重画一次（手动"刷新"），不改 streak 规则 |
| `SHELL_EV_BACK` / `SHELL_EV_HOME` / key `esc`/`back`/`home` | 聚焦视图 → 回总览；总览 → `shell_show_launcher()` |
| 其余 key | 忽略 |

聚焦视图是同一 app 内的一个 `view` 枚举，不是新的刷新 session——切换只是重画内容区 + partial。
两层深（总览/聚焦），`BACK` 逐层退，和 `app_settings.cpp` 的 `PAGE_*` 同款。
聚焦视图可放 U-4 之后的增量；双栏总览是 U-4 主体。

---

## 6. 注册与图标

### 6.1 `assets/generated/icons.h` / `icons.c`

```c
extern const uint8_t SHELL_ICON_USAGE[SHELL_ICON_LEN];   /* 80x80 1bpp panel-native */
```

字形：一个仪表盘半圆 + 指针，或两根竖直进度柱。**原创**，不用吉祥物。
可手写一个小 Python 片段生成，或复用 `tools/` 里已有的 PNG→1bpp 流程。ROM +800B，可忽略。

### 6.2 `src/shell/app_registry.cpp`

```c
{ "usage", "USAGE", SHELL_ICON_USAGE, false,
  app_usage_on_enter, app_usage_on_exit, app_usage_tick, app_usage_on_key },
```

launcher 变 3 图标；第 4 格继续画淡边框占位（`launcher.cpp` 已支持空槽）。

### 6.3 设置页（增量，可放 U-6）

`app_settings.cpp` 的 `ITEMS[]` 加 `"USAGE PUSH"`，`ITEM_COUNT` 6→7，进详情页每 provider 一行：

```
USAGE PUSH
claude  47s   allowed_warning  ok
codex   12s   allowed          ok
```

某 provider 从未收到 → 该行 `never`。`usage_model_seq()==0` → 整屏 `no host`。

---

## 7. 主机脚本 `tools/usage_push.py`（Windows-first）

单文件，依赖 `bleak`（BLE）。**一个脚本进程，一条 BLE 连接，推两个 provider**。
取数方式全部来自 `docs/CLAUDE_CODEX_USAGE_INVESTIGATION.md` §5——**不读任何凭据/DB 文件推断额度**。

内部结构：一个 `Provider` 抽象，`collect() -> dict | None`（`None` = 这轮拿不到，不推该 provider），两个实现。

### 7.1 `ClaudeProvider`

- **主路径**：子进程跑
  `claude -p "/usage" --output-format json --no-session-persistence`，取 JSON 外壳的 `result`
  字符串，正则解析两行：
  `Current session: NN% used · resets <date>` → `s` + `sr`
  `Current week (all models): NN% used · resets <date>` → `w` + `wr`
  `<date>` 是本机时区的人类日期（`Aug 29, 1:19pm`），用 `dateutil` 解析成 epoch，再算
  `分钟差 = (reset - now)/60`。实测 `num_turns:0 / total_cost_usd:0`——**不计费**。
- **回退**（`result` 文本解析失败，或需要更稳的数值）：`GET https://api.anthropic.com/api/oauth/usage`，
  头 `Authorization: Bearer <accessToken>` + `anthropic-beta: oauth-2025-04-20`，
  读 `five_hour.utilization` / `five_hour.resets_at`（ISO8601）/ `seven_day.*`。
  accessToken 从 `%USERPROFILE%\.claude\.credentials.json` 的 `claudeAiOauth.accessToken` 取，
  **只用于这一个请求，绝不打印/落盘**。这是逆向接口，非公开契约——解析器对新增 `limits` 数组、
  缺字段、401/429 全部容错，命中就缓存 + 退避。
- `acct` = `credentials.json` 的 `subscriptionType`（`pro`）。
- `cc`/`ccm`：可选，读 `%USERPROFILE%\.clawd\cc_status.json`（Claude Code hook 写）；没有就不带。
- 调用频率：跟随主循环的推送周期（默认 6h），外加 on-connect 补推。每周期最多 spawn 一次
  `claude`——冷启动数百 ms 在 6h 周期里完全可忽略（这也让 §11 里"逐秒 spawn 开销"的顾虑消失）。

### 7.2 `CodexProvider`

- **主路径**：脚本启动时 spawn 一个常驻 `codex app-server --listen stdio://`，握手
  `initialize` → `initialized`，先发 `account/rateLimits/read` 拿完整快照，之后监听
  `account/rateLimits/updated`（sparse update，和上一次完整快照 merge，或收到就重新 `read`）。
  映射：`rateLimits.primary.usedPercent/resetsAt`（`windowDurationMins≈300`）→ `s`/`sr`；
  `rateLimits.secondary.*`（`≈10080`）→ `w`/`wr`。`resetsAt` 是 Unix 秒。
- **机会性推送**：`account/rateLimits/updated` 触发时，若距上次 Codex 推送 ≥30min，可插一条
  额外的 Codex `usage.push`——这是免费订阅，不占"4 次/天"基线，让 Codex 侧在窗口跳变时更跟手。
- **离线回退**：app-server 起不来时，扫最近的
  `%USERPROFILE%\.codex\sessions\YYYY\MM\DD\rollout-*.jsonl`，从尾部找最后一个非空
  `payload.rate_limits`（`event_msg` / `type:"token_count"`，snake_case）。可能滞后，标记为 stale-ish。
- `st`：Codex 有 `rateLimitReachedType` / `spendControlReached`——映射到我们的 `st`
  （`spendControlReached==true` → `limited`；`rateLimitReachedType` 非空 → `rejected`；否则 `allowed`）。
- `acct` = `planType`（`plus`/`pro`）。无 `cc`/`ccm`。
- 忽略 `rateLimitsByLimitId` 里的额外 bucket（如 `base_model_inference`）——v1 只画 primary/secondary。
- **JSONL 是 snake_case、app-server RPC 是 camelCase**，两套解析路径不要混用。

### 7.3 主循环

扫描名字 `ECO-epaper_154-*` 的设备，连上，协商 MTU ≥ 200。**每次 (重)连上立即跑一轮**，
之后按**脚本启动起算的相对间隔** `--interval`（默认 **21600s = 6h**，非固定钟点、不依赖 cron）
跑一轮：对每个 provider 调 `collect()`，非 `None`
就向 `COMMAND_UUID`（`7e0d0003-…`）write 一条 §4.2 的 JSON（带 `p`，`rid` 自增，`v:1`），
两条之间隔 ~200ms；监听 `RESPONSE_UUID` 收 `code:"OK"`。Codex 的机会性推送（§7.2）走同一
write 路径，独立限流。断连自动重扫重连（刷固件会重启设备）。退出时关掉常驻 app-server 子进程。

**开关**：`--provider claude,codex`（默认两个）/ `--dry-run`（只打印 JSON 不连 BLE）/
`--once`（每 provider 推一次退出）/ `--interval 21600`（秒，默认 6h）/
`--claude-source usage|oauth`（默认 `usage`）/ `--no-opportunistic`（关掉 Codex 的 updated 补推）。

不做终端 dashboard——要看纯 CLI 用各自的 `/usage`。

---

## 8. 落地顺序（每步 `pio run -e epaper_154_app` 干净，尽量实机）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **U-1** 协议 + handler + 串口 | `OP_USAGE_PUSH`/`CAP_USAGE`；`usage_types.h`（含 `p`）；`handle_usage_push` + weak `usage_model_apply` 空桩；`COMMANDS[]` / `add_capabilities()` 各一行。**无 UI**。 | nRF Connect 或 `usage_push.py --once` write `p:"claude"` 和 `p:"codex"` 各一条 → 串口 `[ECP] usage.push p=.. s=..`；`sys.info` capabilities 含 `"usage"`；坏 JSON / 缺 `s` → `BAD_REQUEST`；缺 `p` → 当 claude |
| **U-2** model TU | `usage_model.{h,cpp}` 强符号；provider 定长数组 + 按名命中/建槽 + 字段收敛 + `ccm` 截断 + 全局 `seq` + per-provider `age_ms`。设置页 "USAGE PUSH" 每 provider 一行。 | 交替推 claude/codex 各 3 条 → 两槽独立更新；设置页两行 `last` 各自递增；`ccm` 超 63 字节被截断；重启后两行 `never` |
| **U-3** app 骨架 | `app_usage.{h,cpp}` 4 回调；注册表加一行；`SHELL_ICON_USAGE` 占位图；`on_enter` 遍历 `usage_model_count()` 画静态占位（"NO HOST"），ESC/HOME 返回。 | launcher 3 图标；进/出额度页交接无残影（看 `busy_ms`）；`seq==0` 显示占位 |
| **U-4** 双栏总览渲染 | 每 provider：标题 + 两条进度条 + 倒计时（本地按分钟递减，`sr` 归零画斜纹+`~`）+ per-provider `NO HOST`；合并限流横幅（更严重者）+ CC 行（有 cc 才画）。change-gated 重画 + streak 强制 full + 长按强制 full。 | 推变化的 `s`/`w` → 对应条动，partial `busy` ~300–500ms；不推时每分钟只重画倒计时；`sr` 递减到 0 → 该条转斜纹；age 越过 `USAGE_STALE_MS`（~7h）→ 该槽 `NO HOST`，另一槽照常 |
| **U-4b**（增量）聚焦视图 | tap 上/下半屏 or `left`/`right` 键 → 单 provider 放大视图；`BACK` 回总览 | 切换只 partial，不新起 session；两层 `BACK` 逐层退 |
| **U-5a** Codex 取数 | `CodexProvider`：常驻 `codex app-server` + `account/rateLimits/read` + `.../updated`；回退扫 `rollout-*.jsonl`。`--dry-run` 打印映射后的 JSON。 | `--dry-run --provider codex` 输出的 `s/sr/w/wr` 和 `codex` 里 `/status` 显示一致；app-server 杀掉后自动落到 JSONL 回退 |
| **U-5b** Claude 取数 | `ClaudeProvider`：`claude -p "/usage" --output-format json` 正则解析 + 日期转分钟；回退 `GET /api/oauth/usage`。 | `--dry-run --provider claude` 输出和 `claude` 里 `/usage` 一致；`--claude-source oauth` 也能跑；token 不出现在任何日志/输出 |
| **U-5c** 合流 + BLE | `Provider` 抽象 + 主循环：一进程一连接推两家；`--once` / `--interval` / 自动重连 / 退出清理 app-server。 | Windows 常驻：两栏各自每分钟内跟随真实用量；某家 `collect()` 返回 `None` 时只有该栏 stale；重刷固件后自动重连 |
| **U-6** 文档 + 契约 | `bsp_caps.h` 加 `BSP_CAP_USAGE 1`；契约 bump 0.10.0；`CHANGELOG.md` 一条带 Validation；`EPAPER_154_APP_GUIDE.md` 补 `usage.push` 表 + 脚本运行方式；本文件标"已落地"。 | `pio run -e epaper_154_app -e epaper_154_m0 -e epaper_154_m1` 全绿；CHANGELOG Validation 段写清"BLE 载荷回归 + 主机脚本 Windows 实测" |

U-3 之前纯固件、可离线验证；U-5 才依赖主机端。

---

## 9. 契约与 CHANGELOG

- 新增 `BSP_CAP_USAGE` → 板级 contract **0.10.0**（APP_SHELL_PLAN §11.8 已预留这个号）。
- `boards/epaper_154/CHANGELOG.md` 按现有格式加 `# [0.10.0] - <日期>`，`### Changes` + `### Validation`。
- Validation 必须含：`pio run` 三环境编译；BLE `usage.push` 回归（坏 JSON / 缺字段 / `ccm` 超长 / 大小逼近 256）；
  `usage_push.py` 在 Windows 实测；额度页 partial `busy_ms` 落在 300–500ms。

---

## 10. 风险与注意（承接分析 §5，加 shell 特有项）

| 风险 | 说明 | 缓解 |
|---|---|---|
| Claude `/usage` 的 `result` 是人类文本、非数值 schema | 换行/措辞/时区格式可能随 CLI 版本变，正则会碎 | 正则容错 + 解析失败即回退 `GET /api/oauth/usage`；两路都失败则该轮 `None`（栏位 stale），不崩 |
| `GET /api/oauth/usage` 是逆向接口、非公开契约 | 字段/结构可能变，或返回 401/429 | 只作回退；解析器兼容新增 `limits` 数组 + 缺字段；命中就缓存 + 退避 + stale-cache fallback |
| Codex 无独立限流状态文件 | `state_5.sqlite`/`auth.json` 都没有 rate_limits；只有会话 `rollout-*.jsonl` 里的事件 | 主路径用 `codex app-server` RPC（结构化、camelCase）；`rollout-*.jsonl` 只作离线回退，且标记可能滞后 |
| 两 provider 字段/窗口/bucket 随计划和版本变 | Codex 有 `rateLimitsByLimitId` 额外 bucket；窗口分钟数偶有偏差 | schema 只取 primary/secondary 的 `used_percent + resets_at`；忽略额外 bucket；`st` 渲染层兜底未知值 |
| 误触发计费 | 若脚本改用真实模型请求刷额度就会计费 | 只用 `/usage`（`num_turns:0`）和 app-server `rateLimits/read`（无模型任务）；**禁止** `codex exec` / 真实 `claude -p "<prompt>"` 作探测；脚本不接受 API key 参数 |
| 屏幕挤 | 200×200 上塞两家 4 条 + 横幅 + CC 行 | 紧凑行距（§5.1），倒计时压进条行尾；信息再多走 U-4b 聚焦视图 |
| 载荷逼近 256B | `ccm` 顶格 + 未来加字段会炸 | handler 截 `ccm` 到 63；v1 不推直方图；`warn_if_over_notification_limit` 已在响应侧兜底，命令侧 `onWrite` 已拒 >256 |
| partial 基准图失同步 | 额度页 ↔ launcher 切换、强制 full 时若不走 shell 交接会残影 | app **不碰** `partial_begin/end`；强制 full 只经 `shell_full_refresh_current()`（CLAUDE.md「Partial refresh is session-based」invariant 的唯一执行点） |
| 会话窗（5h）显示过期 | 6h 推送周期 > 5h 窗口，两次推送间 session% 必然滞后、甚至已重置 | 设备本地按分钟递减 `sr`，归零即把 session 条改斜纹 + `~`（不显示旧 %）；on-connect 补推让重启后立刻新鲜；7d 窗不受影响；用户已知并接受 |
| 倒计时刷新累积 | 若做成每分钟跳数字，一天上千次 partial，违背"低频"初衷、加速屏幕老化 | 倒计时只显示粗桶（`>3h/~2h/…/due`），桶标签变了才重画——一天个位数次 |
| OAuth token 安全 | 回退路径读 `.credentials.json` 的 `accessToken` | 只本地用、只发这一个请求，不进日志/输出/落盘；主路径（`/usage` 子进程）根本不碰 token |
| 版权素材 | Clawdmeter 含吉祥物 + 专有字体，未开源 | 参考 `clawdometer-eink`（MIT）；图标原创；不拷贝任何位图/字体 |
| 分层渗漏 | `ecosystem_ble.cpp` 是板级无关层 | 只加 weak 桩 + POD 结构体头；强符号在 `src/apps/usage/`。禁止在 ble 层 include EPD/app 头 |
| `usage_model` 并发 | `handle_usage_push` 在主 loop 上下文（`process_pending_command` 从 `ecp_ble_loop` 调），app 也在主 loop | 单线程，无锁即可；`seq` 用普通 `uint32_t`。若将来 handler 移入 BLE 回调线程需加 `volatile` + 原子 |
| BOOT 长按语义冲突 | 运行期长按 = 回主页；开机窗口长按 = 全诊断 | 已由 shell 处理，额度页无需关心 |

---

## 11. 待确认（不阻塞 U-1，U-4 前定）

已由 `docs/CLAUDE_CODEX_USAGE_INVESTIGATION.md` 解决的旧问题（取数通道、是否计费、有无 headless
输出、Codex 数据源）不再列出。剩余：

- 进度条方向：填充色 = 已用量（越满越危险）确认？还是画"剩余额度"？（倾向前者，和 Clawdometer 一致）
- 默认视图：双栏总览（当前设计）还是启动就单 provider、靠键切？（倾向总览，用户明确要"同时看"）
- `cc`/`ccm` 状态行：Claude 侧没装 hook 时留空还是显示占位？（Codex 侧本就没有此行）
- Claude 取数默认走 `/usage` 子进程还是 `oauth/usage`？（倾向 `/usage`：不碰 token；`oauth/usage` 作回退）
- Codex `updated` 机会性补推默认开还是默认关？（倾向开，限流 30min；`--no-opportunistic` 可关）
- `limited`/`rejected` 是否值得响一声（板子有 ES8311）？本计划先不做，作 U-7 增量。
- 图标最终字形：仪表盘指针 vs 双竖柱——U-3 画的时候看实机对比。
- 直方图：确定不做，还是留 `usage.hist` 分片接口的桩？（倾向确定不做，YAGNI）
- 主机脚本语言：Python（`bleak`）一份；不额外维护 PowerShell 版。
