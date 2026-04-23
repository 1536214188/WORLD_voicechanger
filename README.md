# WORLD_voicechanger

基于 WORLD 声码器的实时变声工程集合。当前主线是 **Android 10ms PCM / JNI `.so` 交付版本**，同时保留了 Python 原型、Windows 实时测试和 C/C++ SDK 实验版本，方便回溯开发过程。

## 项目主线

当前推荐从 Android `.so` 接入链路开始阅读：

```text
app     最终交付给 Android App 的头文件和 so
packet  Android so 编译打包工程
World   WORLD 声码器算法依赖
```

核心接口位于 `app/include/voicechanger.h`：

```c
int vc_init(void);
void vc_set_params(double pitch, double formant);
PCMFrame10ms vc_process(PCMFrame10ms input);
void vc_destroy(void);
```

音频约定：

- 采样率：`48000 Hz`
- 帧长：`10ms`
- 单帧采样数：`480`
- PCM 格式：`int16_t`
- 输入/输出结构：`PCMFrame10ms`

## 快速接入

Android App 侧主要使用以下文件：

- `app/include/voicechanger.h`
- `app/libs/arm64-v8a/libvoicechanger.so`
- `app/libs/armabi-v7a/libvoicechanger.so`

典型调用流程：

```text
vc_init()
vc_set_params(pitch, formant)
循环调用 vc_process(input_frame)
vc_destroy()
```

其中 `pitch` 用于控制音高缩放，`formant` 用于控制共振峰偏移。实际效果建议根据 App 侧听感调参。

## 目录说明

```text
.
├── app                         Android App 交付目录
├── packet                      Android so 打包工程
├── World                       WORLD 声码器源码
├── archive                     历史实验工程和非主线依赖
├── PROJECT_STRUCTURE.md        项目结构详细说明
├── REORGANIZATION_MAP.md       文件整理迁移记录
└── .gitignore                  Git 忽略规则
```

`archive` 下内容主要用于参考：

- `archive/python-prototype`：Python + pyworld 原型。
- `archive/windows-tests`：Windows + PortAudio 实时测试工程。
- `archive/sdk-experiments`：C/C++ SDK、C 接口、10ms/JNI 等历史版本。
- `archive/third-party/portaudio`：Windows 音频输入输出依赖，Android 主线不依赖。

## 编译说明

Android so 打包入口在：

```text
packet/CMakeLists.txt
```

该 CMake 工程会：

- 编译 `packet/world/src` 下的 WORLD 源码为静态库 `world`。
- 编译 `packet/voicechanger/voicechanger.c` 为动态库 `voicechanger`。
- 链接 `world`、`log`、`m`，生成 Android `libvoicechanger.so`。

当前仓库已保留现有 Android 交付产物：

- `app/libs/arm64-v8a/libvoicechanger.so`
- `app/libs/armabi-v7a/libvoicechanger.so`
- `packet/build_android_arm64/libvoicechanger.so`
- `packet/build_android_arm32/libvoicechanger.so`

## 推荐阅读顺序

1. `PROJECT_STRUCTURE.md`
2. `app/include/voicechanger.h`
3. `app/voicechanger.c`
4. `packet/CMakeLists.txt`
5. `packet/voicechanger/voicechanger.c`

如果只是 Android 接入，优先看 `app`。如果要重新打包 `.so`，优先看 `packet`。如果要调整算法效果，重点看 `voicechanger.c` 和 `World/src`。

## 注意事项

- 本仓库已删除 Visual Studio `.vs` 本地缓存目录，后续打开工程会自动重新生成。
- `.gitignore` 会忽略常见中间产物和本地构建文件，但保留 Android 交付 `.so`。
- `World` 和 `portaudio` 已作为普通源码目录纳入本仓库，不再作为 Git 子模块。
- 历史工程中可能存在旧的硬编码路径，移动后不保证直接可运行；可参考 `REORGANIZATION_MAP.md` 找回原始位置。

