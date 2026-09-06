# FILE SYNC 局域网协议

固件启动后会在已配置 Wi‑Fi 上运行单客户端 HTTP 服务：`http://<ip>/`。
UDP `4210` 端口接受 `FILE_SYNC_DISCOVER\n`，返回设备 ID、名称、IP 和 HTTP 端口，
因此电脑端不需要预先知道设备地址。

除 `GET /api/v1/device` 外的 API 都需要请求头：

```http
X-File-Sync-Token: <Settings 中显示的 32 字符 Token>
```

接口：

| 方法 | 路径 | 作用 |
| --- | --- | --- |
| GET | `/api/v1/device` | 发现设备和 SD 容量 |
| GET | `/api/v1/settings` | 读取同步设置 |
| POST | `/api/v1/settings` | 修改设备名/同步策略，可 `rotate_token:true` |
| GET | `/api/v1/files?path=/video` | 列出目录 |
| GET | `/api/v1/file?path=/video/a.bin` | 下载，支持 `Range` |
| PUT | `/api/v1/file?path=/video/a.bin` | 暂存后替换普通文件 |
| DELETE | `/api/v1/file?path=/video/a.bin&confirm=true` | 删除；目录还需 `recursive=true` |
| POST | `/api/v1/rename` | JSON `{ "from": "/a", "to": "/b" }` |
| POST | `/api/v1/sync/prepare` | 提交事务清单 |
| GET | `/api/v1/sync/status?...` | 查询事务文件已接收长度 |
| PUT | `/api/v1/sync/chunk?...` | 写入事务块 |
| POST | `/api/v1/sync/commit` | 校验全部文件并提交 |
| POST | `/api/v1/sync/abort` | 删除暂存并放弃事务 |

事务块使用：

```http
PUT /api/v1/sync/chunk?transaction=sync-1&path=story.json
Content-Range: bytes 1436-2871/12345
```

Raw 上传还应发送等价请求头：普通文件使用 `X-File-Path`，事务块使用 `X-Sync-Transaction` 和 `X-Sync-Path`。
内置 CLI 会自动发送这些头；其他非 raw 请求仍使用 URL query 参数。

每个文件的块 offset 必须等于设备当前暂存长度；网络中断后先调用
`sync/status`，从返回的 `received` 继续。`commit` 前会校验清单中的大小和 SHA-256。
目标目录在提交时才替换；播放锁处于活动状态时提交返回 `409 busy`。

电脑端工具：

```text
python tools/device_sync.py discover
python tools/device_sync.py pair --token <TOKEN>
python tools/device_sync.py list /
python tools/device_sync.py sync ./package /video/odyssey_homecoming
python tools/device_sync.py sync-story .\assets\stories\fox_forest
python tools/device_sync.py sync-library .\assets\stories
```

`sync-story` 先运行本地故事包检查，再以一个独立事务替换
`/video/<story_id>`；`sync-library` 对每个子目录分别执行，因此某个包
失败不会影响设备上已经提交的其他故事。事务清单中的每个文件都使用
SHA-256，断线后客户端从 `/api/v1/sync/status` 返回的 `received` 偏移
继续上传。提交时设备再次验证故事包；播放租约存在时提交返回 `409 busy`。

## 同步测试经验与故障排查

以下规则来自实际设备测试，后续修改同步或播放器时必须保留：

1. 路径有两个命名空间。`file_storage` 和 HTTP API 使用 `/sdcard/...` 的规范路径，
   `SD_MMC.open()` 使用挂载点内的相对路径，例如 `/video/...`。不能把
   `/sdcard/video` 直接传给 `SD_MMC.open()`，否则会变成
   `/sdcard/sdcard/video`，导致故事列表为空。所有直接访问 `SD_MMC` 的代码都应
   通过 `file_storage::internal::to_sd_path()` 转换。
2. 编译成功不能证明 TF 播放可用。每次改动后至少要在真实设备执行一次：进入
   `PHOTOS`、查看串口中的 `SD scan` 和 `scan found`，再实际打开一个 FVID。
3. HTTP 任务栈很小。完整故事清单不能放在 `handle_prepare()` 的局部栈数组中，
   否则会触发 `file_sync_http` stack canary。大清单应使用静态存储或堆内存，且
   仍需检查任务栈余量。
4. WebServer raw 上传会把一个 HTTP body 拆成多个内部回调。若每个回调都打开、
   flush、关闭一次 TF 文件，吞吐会极慢并容易超过客户端超时。主机块应合并后再写入；
   大缓冲放 PSRAM，不能盲目放内部 RAM，否则会导致 Wi-Fi RX buffer 初始化失败。
5. 故事验证应以 `story.json` 的实际引用为准：引用的 FVID、图片和音频必须存在且
   格式正确；未引用的预览图、source 音频和普通文件不能阻塞提交。缺少被引用音频
   可以报告 warning，并允许画面播放。
6. `commit` 验证失败后事务可能仍处于 PREPARED/active 状态。客户端必须支持
   `status` 续传或显式 `abort`，不能直接开始另一个故事；`sync-library` 必须保证
   一个故事失败不影响其他故事，并能安全重跑已提交的相同清单。
7. 验收顺序应包含：准备事务、上传首块、查询 `received`、续传、故意 SHA 错误提交、
   abort、重启，再检查旧版本和目标目录。只检查 HTTP 200 或只检查本地资源是不够的。
8. 设备日志中的 `NO STORIES AVAILABLE` 应优先追查扫描路径和 `story.json` 校验，
   不应先假设需要重启；重新进入 App 应触发扫描，提交 generation 变化也应触发刷新。
