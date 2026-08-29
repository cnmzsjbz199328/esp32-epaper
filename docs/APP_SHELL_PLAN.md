# App Shell 重构计划：图标主页 + 应用注册 + 自检收进设置

> 制定日期：2026-08-29
> 目标：把当前"编译期 `APP_MODE` 三选一"改成"图标启动器主页 + 可扩展应用注册表"，
> 让 `app_family_video` 成为一个从 SD 卡加载内容的独立应用，硬件自检和 M0/M1 走查
> 收进"设置"应用按需运行。参考 `esp32-lcd154` 的 `companion_apps`（`AppRouter` + 每应用一目录），
> 但按电子纸的刷新模型改造，不照搬 LVGL 每帧重绘那套。

## 已锁定的决定（用户确认）

| 项 | 决定 |
|---|---|
| 主页形态 | **图标网格 2×2**，短期 3–4 个应用；顶部常驻状态栏显示 SHTC3 温湿度 |
| 返回上一级 | **BOOT 长按**回主页；同时接受 Cardputer 经 BLE 的按键，**ESC = 返回上一级** |
| 图标位图 | 编译进 `assets/generated/`（1bpp，panel-native） |
| SD 相框格式 | 见 §4，采用固定帧长 `FVID` 容器 |
| 后台自检 | **默认不跑**；进"设置 → 硬件自检"那一行时才按需运行 |
| M0 / M1 走查 | 也收进"设置"，不再是编译期 env（`epaper_154_m0/m1` 环境可保留供 CI 编译） |

## 关键约束（决定了不能照搬 lcd154）

1. **电子纸没有每帧重绘**。`esp32-lcd154` 的 `IApp::onRender(GfxCanvas&)` 是每帧模型；这里应用是**事件驱动**：等一个输入事件 → 改 fb → 一次 partial/full refresh。应用代码形态接近现在的 `app_family_video.cpp::app_wait_nav()`。
2. **切换应用 = 一次刷新 session 交接**，必须集中在一处执行（CLAUDE.md「Partial refresh is session-based」）：
   `partial_end() → fb_clear → 画状态栏 → fb_flush_full() → partial_begin() → app.on_enter()`。
   应用自己**不碰** `partial_begin/end`。
3. **`selftest.cpp` 不是并发安全的**：它会自己 install/delete I2C driver、直接驱动 EPD、含多处 ~1s 阻塞（RTC 读两次、触摸窗口、EPD 刷新）。所以自检是**模态**运行（前台应用挂起 → 跑完 → 恢复），不是 lcd154 那样的常驻 `DiagService` FreeRTOS 任务。用户选的"按需运行"正好化解了这个矛盾。
4. **SHTC3 目前没有公开 BSP API**，读取逻辑只存在于 `selftest.cpp`。状态栏需要先把它抽成正式板级驱动（照 v0.7.7「收敛为正式板级接口」的先例，像 `touch.cpp`/`sdcard.cpp`）。

---

## 1. 模块布局

```
src/
  main.cpp                     # setup: board/ui/touch/BLE init -> shell_begin(); loop: shell_tick()
  shell/
    shell.h  shell.cpp         # 协调器：活动屏、输入路由、刷新 session 交接、BOOT长按/ESC=返回
    app_registry.h  .cpp       # 静态表 app_entry_t[]，加应用只动这张表
    status_bar.h  .cpp         # 顶部 ~26px：温湿度 / BLE 链路 / 电池
    launcher.h  .cpp           # 图标网格屏（2×2）+ 选中态 + 进入
  apps/
    iapp.h                     # app_entry_t 结构（C 函数指针表）
    family_video/
      app_family_video.h  .cpp # 由 src/app_family_video.* 迁入，只保留"显示相框+翻页"
      frame_source.h           # 抽象：count() / hint(i) / load(i, fb)
      frame_source_sd.cpp      # 读 /sd/video/*.fvid
      frame_source_rom.cpp     # 包 family_video_assets.c，作 SD 失败时的兜底
    settings/
      app_settings.h  .cpp     # 列表屏（列表在设置内部 OK，主页才是图标）
      diag_runner.h  .cpp      # 模态跑 selftest + M0/M1，结果存进 AppState
    usage/                     # 未来：见 docs/CLAWDOMETER_ANALYSIS.md
  app_diagnostics.h  .cpp      # 保留 M0/M1/BOOT 全测试入口，改由 diag_runner 调用
assets/generated/
  icons.h  icons.c             # 新增：每个应用一个 1bpp 图标数组
```

`lib/` 不新增大件。`lib/bsp_core/` 可能需要一处**加法式**改动（自检结果落到一个 RAM ring，见 §5）。

---

## 2. 应用接口 `apps/iapp.h`

沿用 lcd154 的生命周期命名，但按电子纸改成事件驱动、C 函数指针表（无 C++ 虚表、无堆分配）：

```c
typedef struct app_entry {
    const char*    id;          // "family_video"
    const char*    title;       // "相框"
    const uint8_t* icon;        // ICON_W*ICON_H/8 字节，1bpp panel-native
    bool           fullscreen;  // true=占满 200x200，隐藏状态栏；false=内容区在状态栏下方
    void (*on_enter)(void);     // shell 已完成 full-refresh 交接、partial session 已 begin
    void (*on_exit)(void);      // shell 收回屏幕前
    void (*tick)(void);         // 活动期间反复调用；内部用 shell_wait_event() 等输入
    void (*on_key)(const char* key, const char* event);  // 非 HOME/BACK 的按键转发到这里
} app_entry_t;
```

**加一个应用 = 新建 `apps/<id>/` + `app_registry.c` 加一行 + `icons.c` 加一个数组。** 无 switch。

---

## 3. 输入模型：shell 拥有唯一的等待函数

```c
typedef enum {
    SHELL_EV_NONE, SHELL_EV_TIMEOUT,
    SHELL_EV_TAP,          // 带 x,y,long 标志
    SHELL_EV_KEY,          // 带 key/event，已排除 HOME/BACK
    SHELL_EV_HOME,         // BOOT 长按（运行期）
    SHELL_EV_BACK,         // ESC / "back"
} shell_ev_kind_t;

shell_event_t shell_wait_event(uint32_t timeout_ms);
```

`shell_wait_event()` 内部（从现在的 `app_power_poll` / `app_wait_nav` 提炼）：

- 每轮调 `ecp_ble_loop()`，保持 BLE 在长刷新期间不超时（现有注释里 esp32_test 踩过的坑）。
- 轮询 PWR：长按 3s → 清屏 → `partial_end` → `fb_flush_full` → `bsp_power_off()`（从 family_video 提上来）。
- 轮询 BOOT：运行期长按 >1500ms → 返回 `SHELL_EV_HOME`。
- 排空 Cardputer 按键队列：`bsp_remote_key_event()`（**改由 shell 拥有**，不再在 `app_family_video.cpp`）把按键推进一个小环形队列。
  - `esc` / `back` → `SHELL_EV_BACK`
  - `home` → `SHELL_EV_HOME`
  - 其余（`left/right/up/down/enter/...`）→ `SHELL_EV_KEY`，由 shell 转给 `active_app->on_key`
- 触摸 tap（沿用现有防抖/稳定判定）→ `SHELL_EV_TAP`。

**返回语义（"上一级"）**：
- 在应用里：`BACK` == `HOME`（当前只有一层深）。
- 在设置的子屏（如自检结果页）：`BACK` 先弹一层子屏，再到设置列表，再到主页。
- 在主页：`BACK` 无操作（或进休眠，待定）。

**`handle_input_key` 需要 1 行改动**：现在白名单收 `"back"` 不收 `"esc"`（`lib/ecosystem_ble/ecosystem_ble.cpp:476`）。加 `esc` 别名，shell 里 `esc`/`back` 同义。

---

## 4. 刷新 session 交接（集中在 `shell_switch_to`）

```c
static void shell_switch_to(const app_entry_t* next) {
    if (bsp_ui_partial_active()) bsp_ui_partial_end();
    if (s_active) s_active->on_exit();

    s_active = next;
    bsp_ui_fb_clear(0xFF);
    if (!next || !next->fullscreen) status_bar_render_into_fb();   // 顶部 ~26px
    bsp_ui_fb_flush_full();          // 每次换屏一次 full refresh
    bsp_ui_partial_begin();          // 新基准图 = 状态栏 + 空白内容区

    if (next) next->on_enter();      // 应用用 partial refresh 画内容区
}
```

- 这是 CLAUDE.md 那条 invariant 的**唯一**执行点。
- `fullscreen=true` 的应用（`family_video`）拿整块 200×200，不画状态栏。
  → **理由**：200×200 的相框帧是按整屏排版的；"常驻状态栏"落在主页和设置，全屏应用退出到主页时立刻能看到温湿度。若以后想每屏都留状态栏，需把相框素材重排成 200×174 并给 `frame_source` 加行偏移——留作后话，先用 `fullscreen` 标志。
- `family_video` 现在 `memcpy` 整个 5000 字节 fb——`fullscreen` 下没问题；一旦想塞进状态栏下方就要改。

---

## 5. SD 相框格式（推荐：`FVID` 固定帧长容器）

`tools/video2c.py` 已经把帧打包成 panel-native 5000 字节。加一个 `--format fvid`（或新脚本 `tools/video2fvid.py`）输出单文件容器：

```
/sd/video/<name>.fvid
  偏移 0   magic     "FVID"        4 B
  偏移 4   version   u8 = 1
  偏移 5   flags     u8
  偏移 6   w         u16 = 200 (LE)
  偏移 8   h         u16 = 200
  偏移 10  count     u16
  偏移 12  reserved  4 B (0)
  --- 帧 i，从 偏移 16 + i*5004 起 ---
    hint   u8   0=partial 可, 1=full, 2=final-full   (= APP_VIDEO_REFRESH_HINTS)
    pad    3 B  (0)
    data   5000 B  panel-native，正好 bsp_ui_fb_len()
  文件大小 == 16 + count*5004   ← 唯一需要的完整性校验
```

- **固定帧长 → O(1) 随机访问**：`seek(16 + idx*5004)`，不需要帧索引表。
- **不压缩**：SD_MMC 1-bit 读 5KB 只几毫秒；压缩会加代码和失败面（对照退役的 mask/双 bit-plane 教训——保持扁平）。
- `frame_source_sd.cpp` 实现 `count()/hint(i)/load(i,fb)`；任何错误（无卡 / magic 错 / 大小不符 / 短读）都 log 并让应用回落到 `frame_source_rom`（编译进的 `family_video_assets.c`，保证无卡也有东西看）。
- **懒加载**：翻页时才 `seek+read` 一帧，不在 `on_enter` 一次性读完，避免拖慢进入。
- 卡上多个 `.fvid` = 多个故事；先取 `default.fvid` 或字典序第一个，故事选择 UI 留作增量。

---

## 6. 状态栏 `shell/status_bar.cpp`

- 顶部约 26px：左 `23.4°C 60%RH`（SHTC3），右 BLE 链路图标 + 电池 %。
- 新 BSP API：`bool bsp_shtc3_read(float* t_c, float* rh);` —— 从 `selftest.cpp` 的 SHTC3 段（non-stretching `0x7866` + 等 15ms + CRC8 校验）抽进 `boards/epaper_154/shtc3.cpp`，`bsp.h` 暴露声明。
- 刷新节奏：进主页读一次；在主页时每 N 秒（如 30s）partial refresh 只重画状态栏区域。应用内不刷状态栏。
- 契约：加 `bsp_shtc3_read` → 板级 contract bump 到 0.9.0，`CHANGELOG.md` 带 Validation 段。

---

## 7. 设置应用 `apps/settings/`（内部是列表）

竖直列表屏，行：

| 行 | 动作 |
|---|---|
| 硬件自检 | `diag_runner_run_full()`：模态。挂起前台 → `bsp_selftest_begin(); bsp_selftest_board_specific(); bsp_selftest_summary();`，每条 `bsp_selftest_report` 落进 AppState 的结果 ring → 画结果屏（`N PASS / M WARN / K FAIL` + 可滚动逐项）。约 10–20s，期间显示"检测中…"。 |
| M0 静态帧 | `app_run_m0_diagnostics()`（已存在） |
| M1 局部刷新走查 | `app_run_m1_diagnostics()`（已存在；含 10s 延时 + 20 步，验收数据在串口）。显示"运行中…" |
| BOOT 全测试 | `bsp_epaper_154_interactive_test_app()`（按住 BOOT 开机进的那个），可选保留 |
| WiFi | 显示 `nvs_settings` 里的当前 SSID；"经 BLE config.wifi 设置"，本屏只读 |
| 额度推送 | （usage 应用落地后）最近一次 `usage.push` 时间 / 连接状态 |
| 关于 | `bsp_version_string()`、contract 版本、BLE MAC |

**`diag_runner` 要点**：
- `selftest.cpp` 会 install/delete 自己的 I2C driver 并直接驱动 EPD。模态跑完后必须：恢复 I2C、重新 `bsp_touch_init()`、`status_bar` 重新读 SHTC3，然后 full-refresh 交接回设置列表。
- 需要 `lib/bsp_core` 的自检汇报**加法式**改动：`bsp_selftest_report()` 除了打印+计数，再往一个 RAM ring 追加 `{item, outcome, detail[64]}`（照 lcd154 `DiagResult` 的形状）。供结果屏读取。
- **这是整个重构最高风险的集成点**，单独一条 Validation。

---

## 8. `main.cpp` 重构后

```c
void setup() {
    bsp_board_init();
    bsp_ui_init();
    Serial.begin(115200); /* 等 USB CDC */

    if (app_boot_held_for_diagnostics()) {          // 保留开机按 BOOT 的逃生入口
        bsp_touch_init();
        app_run_boot_diagnostics();
        return;
    }
    bsp_touch_init();
    /* BLE 初始化从 family_video 提上来，带 3 次退避重试 */
    ecp_ble_begin_with_retry();

    shell_begin();     // 画主页 launcher，full refresh，partial_begin
}

void loop() { shell_tick(); }   // 主页时 launcher 处理网格导航；应用中时 active->tick()
```

BOOT 双重职责：**开机窗口**内按住 = 全诊断 app（逃生阀）；**运行期**长按 = 回主页。时间窗不同，不冲突，但要在 `docs/EPAPER_154_APP_GUIDE.md` 写清。

---

## 9. 启动器渲染 `shell/launcher.cpp`

- 进主页：full refresh（状态栏 + 2×2 图标 + 图标 0 的选中框）。
- 触摸某格 → 进对应应用。
- Cardputer 方向键移动选中框（partial refresh 旧格+新格），`enter`/tap = 启动。让键盘用户也能导航。
- 内容区 200×174（状态栏下）；四格建议 80×80 图标居中在 ~88×82 的格子里。
- 空槽位画淡边框占位（当前只有 3 个应用）。

---

## 10. 落地顺序（每步 build 干净，尽量实机验证）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M-A** 抽壳，行为不变 | 把 BLE 初始化 / PWR 轮询 / `bsp_remote_key_event` / 触摸等待助手从 `app_family_video.cpp` 移进 `src/shell/`。`family_video` 变成注册表里一个 `app_entry_t`。**还没有 launcher**，shell 直接进 family_video。 | 行为和今天完全一致：翻页、first/final、BLE left/right、PWR 关机 |
| **M-B** 帧源抽象 | 引入 `frame_source.h`，把 `family_video_assets.c` 包成 `frame_source_rom`。应用改为通过它取帧。无 SD。 | 行为不变 |
| **M-C** SD 帧源 + FVID 工具 | `video2c.py --format fvid`、`frame_source_sd.cpp`、失败回落 ROM。卡上放一个 `.fvid`。 | 从 SD 帧启动；拔卡 → ROM 兜底；串口打印用的是哪个源 |
| **M-D** 启动器 + 状态栏 | `launcher.cpp`、`status_bar.cpp`、抽 `bsp_shtc3_read()`。注册表含 `family_video` + 一个 `settings` 桩。2×2 网格。BOOT 长按 / ESC → 主页。 | 主页 ↔ 相框切换；交接无残影（按 M1 标准看 busy_ms）；状态栏显示实时温湿度 |
| **M-E** 设置应用 + diag_runner | 列表屏；接 硬件自检（模态 + RAM 结果捕获）、M0、M1、BOOT 全测试、WiFi 状态、关于。 | 从设置跑自检 → 结果屏 → 回主页且总线已恢复、无残影 |
| **M-F**（后续） | 额度应用：见 `docs/CLAWDOMETER_ANALYSIS.md` —— `OP_USAGE_PUSH`、`CAP_USAGE`、Mac 脚本、两条进度条 | Mac 脚本推 `usage.push`，进度条随 `s`/`w` 变化 partial refresh |

每个里程碑 → `boards/epaper_154/CHANGELOG.md` 一条，带 Validation 段；涉及新 `BSP_CAP_*` / 新 BSP API 时 bump contract。

---

## 11. 风险与注意

1. **selftest 非并发安全**、会重配 I2C + EPD。模态运行，`diag_runner` 负责存/恢复总线 + 重 `bsp_touch_init()` + full-refresh 交接。最高风险点。
2. **状态栏 vs 全屏相框**：推荐 `fullscreen` 标志，相框隐藏状态栏。若不接受需重排素材成 200×174。
3. **`family_video` memcpy 整帧 5000 字节**：`fullscreen` 下 OK；进状态栏下方需要行偏移 + 重排素材。
4. **SD_MMC 与 EPD/I2C 不共用引脚**，但首次 `bsp_sd_*` 延迟和无卡处理不能卡启动 —— 懒加载，别在 setup 读全部帧。
5. **ESC 键**：`handle_input_key` 收 `"back"` 不收 `"esc"` —— 加 `esc` 别名（1 行），shell 里同义。
6. **BOOT 双重职责**：开机窗口 = 全诊断；运行期长按 = 回主页。时间窗不同不冲突，写进文档。
7. **ROM 预算**：4 个 80×80 图标 = 3.2KB，可忽略。`family_video_assets.c` ROM 兜底继续编译（数十 KB）—— 除非出现 flash 压力否则保留。
8. **契约 bump**：`bsp_shtc3_read` → 0.9.0；后续 `BSP_CAP_USAGE` → 0.10.0。保持 CHANGELOG Validation 纪律。

---

## 12. 待确认（不阻塞开工，M-D 前定）

- 全屏相框隐藏状态栏（推荐）是否接受？
- 主页 `BACK` 行为：无操作 / 进休眠？
- 图标尺寸最终定 80×80 还是 72×72？（M-D 画的时候看实机）
- `.fvid` 多故事：先固定 `default.fvid`，故事选择 UI 是否要进 M-F 之前？
