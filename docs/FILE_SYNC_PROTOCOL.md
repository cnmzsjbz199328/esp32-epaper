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
```
