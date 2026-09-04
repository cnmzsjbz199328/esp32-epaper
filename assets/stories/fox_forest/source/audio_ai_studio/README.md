# AI Studio 音频来源

这些文件来自 Google AI Studio Gemini 3.1 Flash TTS Preview，使用
Achernar 声音生成。下载时的文件名按生成顺序映射为场景编号：

| 场景 | 下载文件 | 项目文件 | 时长 |
| --- | --- | --- | ---: |
| 000 | `Generated Audio September 04, 2026 - 11_27PM.wav` | `audio/000.wav` | 23.96 s |
| 001 | `Generated Audio September 04, 2026 - 11_28PM.wav` | `audio/001.wav` | 14.32 s |
| 002 | `Generated Audio September 04, 2026 - 11_29PM.wav` | `audio/002.wav` | 15.04 s |
| 003 | `Generated Audio September 04, 2026 - 11_30PM.wav` | `audio/003.wav` | 15.80 s |
| 004 | `Generated Audio September 04, 2026 - 11_30PM (1).wav` | `audio/004.wav` | 14.92 s |

原始下载文件保持 24 kHz、16-bit、单声道 PCM；`converted_16k/` 保存了供
固件使用的 16 kHz 转换中间文件。最终播放文件位于同级故事目录的
`fox_forest/audio/`。
