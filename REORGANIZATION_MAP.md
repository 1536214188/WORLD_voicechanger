# voice_project 重组映射

本文档记录本次文件结构调整，便于后续核对或回退。

## 保留在根目录

| 路径 | 说明 |
| --- | --- |
| `app` | Android App 最终交付目录 |
| `packet` | Android so 编译打包工程 |
| `World` | Android 主线使用的 WORLD 声码器算法依赖 |
| `PROJECT_STRUCTURE.md` | 整理后的项目结构说明 |
| `REORGANIZATION_MAP.md` | 本次重组映射 |
| `.gitignore` | 后续 GitHub 上传忽略规则 |

## 移动清单

| 原路径 | 新路径 |
| --- | --- |
| `anytime.py` | `archive/python-prototype/anytime.py` |
| `anytime_out.py` | `archive/python-prototype/anytime_out.py` |
| `VoiceChangerProject` | `archive/windows-tests/VoiceChangerProject` |
| `VoiceChangerProject2` | `archive/windows-tests/VoiceChangerProject2` |
| `VoiceChangerProject3` | `archive/windows-tests/VoiceChangerProject3` |
| `voice` | `archive/sdk-experiments/voice` |
| `voice_c` | `archive/sdk-experiments/voice_c` |
| `voice_c2` | `archive/sdk-experiments/voice_c2` |
| `voice_c_10ms` | `archive/sdk-experiments/voice_c_10ms` |
| `voice_c_10ms_jni` | `archive/sdk-experiments/voice_c_10ms_jni` |
| `voicechanger_sdk` | `archive/sdk-experiments/voicechanger_sdk` |
| `voicechanger（仅world功能）` | `archive/sdk-experiments/voicechanger（仅world功能）` |
| `portaudio` | `archive/third-party/portaudio` |

## 删除清单

以下目录是 Visual Studio 本地缓存，不是源码；本次已删除。

| 已删除路径 |
| --- |
| `VoiceChangerProject/.vs` |
| `voicechanger_sdk/.vs` |
| `voicechanger（仅world功能）/.vs` |
| `voice_c2/.vs` |
| `voice_c_10ms/.vs` |

## 未执行事项

- 未修改 C/C++ 源码。
- 未修改 `packet/CMakeLists.txt` 的构建逻辑。
- 未删除 Android 交付 `.so`。
- 未初始化 Git。
- 未创建 GitHub 仓库。
- 未推送到 GitHub。

