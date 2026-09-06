# 奥德赛电脑端预览器

这是一个只服务于 `family_video` app 的最小验证工具。它读取同一个故事包，显示 FVID 的实际解码帧、场景文字和音频，并记录以下顺序：

```text
display request -> refresh complete -> audio start
```

## 启动

在仓库根目录运行：

```powershell
python tools/odyssey_preview.py
```

只检查资源、不打开窗口：

```powershell
python tools/odyssey_preview.py --check
```

默认读取 `assets/stories/odyssey_homecoming/`。当前 Windows 版本使用系统自带的 `winsound` 播放 WAV，图像显示需要 Pillow；项目已有的故事检查工具也使用 Pillow。

## 使用重点

- 中间区域显示的是 FVID 解码后的 200×200 帧，不是源 PNG。
- 点击场景或使用左右方向键，可以单步检查图、文、音是否对应。
- “自动播放”会执行开幕、14 个场景和最后 3 秒闭幕覆盖。
- “刷新模拟”默认按 FVID 提示模拟：局刷 400 ms，全刷 1755 ms。
- 顺序日志用于观察刷新完成后才开始播放音频。
- “显示检查结果”会报告场景数、FVID 帧数、文件引用和当前固件按编号寻找音频时可能存在的差异。

这个工具只验证内容映射和应用层时序，不代表真实 e-paper BUSY、SD 卡读延迟或 ES8311 音频输出已经通过实机验证。
