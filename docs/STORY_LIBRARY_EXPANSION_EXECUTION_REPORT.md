# 故事库扩展执行报告

## 已完成

- Phase 0：新增 `tools/check_story_assets.py`，检查 story 元数据、编号连续性、图片尺寸、FVID geometry、每帧 `ink_ratio`、WAV 格式和时长。
- Phase 1：新增 `src/apps/family_video/story_catalog.h/.cpp`，把 `PHOTOS` 与 `FOX_FOREST` 的元数据、帧源和 ROM 音频映射从播放逻辑抽出；列表使用稳定 ASCII ID，并打印条目数量、ID、场景数和 ROM/SD 来源。
- SD 读取失败时明确回落到 `ROM:FOX_FOREST`，同时停止原 SD 音频，避免串源。
- `FOX_FOREST` 已替换为白底稀疏线稿，重新生成 FVID 和 ROM ADPCM 资源；原有故事音频未更改。
- Phase 2 内容与视觉：三部故事均已扩展为 14 场景完整情节压缩改写，完成逐字对应的 TTS 脚本、14 张 200x200 图片和 FVID。
- 已新增 `docs/STORY_LIBRARY_EXPANSION_SCENE_MAP.md`，将三套六场景图片映射到约 14 场景的完整情节结构，并标出每个故事预计补充的 8 张转折/冲突图片。
- `odyssey_homecoming` 的旧六场景音频仍仅作为 legacy 底稿保留，不与扩展脚本混用，也不进入正式 SD 包。
- 已从 Downloads 找到新版《奥德赛》的 14 个 MiniMax WAV，按文件实际下载时间（`LastWriteTime`，升序）映射为 000–013；旧批次文件被排除。原始 32 kHz/16-bit/mono 文件归档于 `source/audio_minimax/expanded_14_scene/`，映射、来源文件名和时长记录在该目录的 `manifest.json`。
- 14 段音频已转换为设备使用的 16 kHz/16-bit/mono WAV，写入 `odyssey_homecoming/audio/000.wav`–`013.wav`，并将 `audio_status` 更新为 `ready`。本次未使用背景音乐或 sound tag，停顿由中文标点自然产生。
- 独立开幕音频已按同样流程归档为 `source/audio_minimax/expanded_14_scene/opening.wav`，转换为 `odyssey_homecoming/audio/opening.wav`；原始 Downloads 文件已移动，不再残留在 Downloads。`story.json` 已记录开幕显示信息和闭幕最后 3 秒覆盖配置。
- 固件已接入奥德赛开闭幕时序：检测到 `audio/opening.wav` 后先显示 `THE ODYSSEY / HOMER` 并播放开幕音频，结束后才开始场景 000；最后一幕音频剩余约 3 秒时显示 `OVER / WRITER: TOM`，不切换或新增闭幕音频。
- 已移除三个新故事的本机 SAPI 备用音频、两个未定稿故事的临时音频文件以及本机 TTS 生成脚本；每个故事只保留正式音频槽位，待选定独立人声后再写入。
- 新增 `docs/STORY_ASSET_AGENT_GUIDE.md`，固定图片提示词、检查命令、音频交接和 SD 复制规范。
- 新增 `tools/stage_story_sd.ps1`：先运行资产检查，并拒绝 `audio_status=pending` 的故事，避免未完成故事进入正式 SD 包。
- 新增 `tools/check_story_library.py`：一次检查全部故事包，并汇总正式可用与待音频状态。

## 验证记录

```text
python tools/check_story_assets.py assets/stories/fox_forest             PASS
python tools/check_story_assets.py assets/stories/odyssey_homecoming     PASS (14 scenes, warnings=0, errors=0)
python tools/check_story_assets.py assets/stories/midsummer_dream        PASS (audio pending warnings)
python tools/check_story_assets.py assets/stories/tempest_island         PASS (audio pending warnings)
python tools/check_story_library.py assets/stories                       PASS (2 ready, 2 pending_audio)
powershell -File tools/stage_story_sd.ps1 -StoryDir assets/stories/odyssey_homecoming -OutputDir assets/staging/odyssey_homecoming_with_opening_20260906  PASS
pio run -e epaper_154_app                                               SUCCESS (RAM 22.7%, Flash 56.2%)
pio run -e epaper_154_app -t upload --upload-port COM9                  SUCCESS (ESP32-S3-PICO-1)
serial boot log on COM9                                                 PASS (no panic/abort/error/reset loop)
pio run -e epaper_154_story_demo                                         SUCCESS (incremental build)
```

三个新故事的扩展脚本和补图已完成；其中 `odyssey_homecoming` 的 14 段正式音频已完成并通过检查，`midsummer_dream` 与 `tempest_island` 仍待分别选择人声并生成音频。检查器会将 pending 故事的缺失音频标为警告，并避免未完成故事被误当作可直接复制到 SD 卡的正式包。

## 尚待实机验收

固件已连接并烧录到实体板，但当前电脑没有枚举出 SD 卡盘符，因此尚未完成有 SD 卡时的故事选择、开幕音频、场景音频切换、末 3 秒闭幕覆盖、刷新耗时和实机照片/视频验收。请把 staging 中 `video/odyssey_homecoming/` 复制到 SD 卡 `/video/odyssey_homecoming/`，然后运行计划中的开幕、首场景、中间场景、末场景和返回列表检查。

最新验收：`pio run -e epaper_154_app` 成功，RAM 22.7%，Flash 56.2%；目标板识别为 COM9 / ESP32-S3-PICO-1，上传校验通过；启动日志未见 panic、abort、error 或复位循环。

## Phase 3 离线评估

- 当前统一列表最多显示 6 项；内置 2 项加首批 3 个 SD 故事共 5 项，尚未触发分页条件。
- 列表保持 ASCII `id` 与场景数，年龄、作者和声音等字段留在各自 `story.json`，不挤占 200x200/5x7 字体的列表宽度。
- 当前 ROM demo Flash 使用率为 56.1%；新经典故事继续放在 SD，不把长音频静态加入 ROM。是否影响启动稳定性仍需实体板验证。
- 分发工具已用 `odyssey_homecoming` 完成一次正式包 staging 验证；对两个 pending 音频故事会主动拒绝 staging。
