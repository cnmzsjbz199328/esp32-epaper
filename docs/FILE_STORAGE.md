# 通用 SD 文件存储层

`lib/file_storage/` 是 SD 文件访问的边界。后续 HTTP 服务、`FILE SYNC`
App 和内容验证器只能通过它访问文件，不应直接调用 `SD_MMC`。

## 路径规则

- SD 固定挂载在 `/sdcard`。
- API 接受 `/sdcard/...`，也接受协议使用的 SD 相对绝对路径，例如 `/video/a.fvid`。
- `..`、反斜杠、冒号和控制字符会被拒绝；输出路径始终是 `/sdcard/...` 的规范路径。
- `/sdcard` 本身不可删除。
- 删除目录必须同时传入 `recursive=true, confirmed=true`；第一版没有格式化接口。

## 事务上传

`FileTransaction` 以目标目录为版本边界。`begin()` 接收文件清单（相对路径、大小、
SHA-256），文件块先写入：

```text
/sdcard/.sync_staging/<transaction>/payload/
```

`write_chunk()` 要求续传 offset 等于暂存文件当前长度；`verify()` 会检查清单中的
每个文件。只有校验全部通过，`commit()` 才会把旧目录移到
`.sync_backup/<transaction>/`，将暂存 payload 重命名为正式目录，并写入
`.sync_versions/<transaction>.commit`。任一步失败都会恢复旧目录，暂存目录随后删除。

状态文件位于 `.sync_transactions/<transaction>.state`，目前状态为
`PREPARED`、`COMMITTING`、`COMMITTED` 或 `ABORTED`，供后续启动恢复和 HTTP 服务使用。

## 播放锁

SD 故事打开后通过 `playback_begin()` 建立逻辑读租约。删除、重命名、复制和正式文件
写入会返回 `Busy`，因此同步可以继续把数据写到 `.sync_staging`，但不能替换正在播放
的内容。离开播放器或切换回 ROM 时必须调用 `playback_end()`。
