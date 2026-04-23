# voice_project 项目结构说明

本项目是一个基于 WORLD 声码器的实时变声工程集合。整理后，根目录以 **Android 10ms PCM / JNI `.so` 交付链路** 为主线，历史实验工程统一放入 `archive`。

## 当前目录结构

```text
D:\voice_project
├── app
├── packet
├── World
├── archive
│   ├── python-prototype
│   ├── windows-tests
│   ├── sdk-experiments
│   └── third-party
├── PROJECT_STRUCTURE.md
├── REORGANIZATION_MAP.md
└── .gitignore
```

## Android 主线

### app

`app` 是最终发给 Android App 使用的交付目录。

- `app/include/voicechanger.h`：App 或上层 Native 接入需要看的 C 接口头文件。
- `app/voicechanger.c`：当前较完整的 10ms 实时变声实现，包含缓冲、后台 WORLD 处理线程和 Android JNI 导出。
- `app/libs/arm64-v8a/libvoicechanger.so`：64 位 Android so。
- `app/libs/armabi-v7a/libvoicechanger.so`：32 位 Android so。
- `app/介绍.txt`：说明该目录是最终发给 App 的 Android so 包。

### packet

`packet` 是 Android so 编译打包工程。

- `packet/CMakeLists.txt`：Android/CMake 构建入口。
- `packet/voicechanger/voicechanger.c`：参与 Android so 构建的变声实现源码。
- `packet/voicechanger/voicechanger.h`：参与 Android so 构建的接口头文件。
- `packet/world/include`、`packet/world/src`：打包工程内复制的一份 WORLD 头文件和源码。
- `packet/build_android_arm64/libvoicechanger.so`：arm64 构建产物。
- `packet/build_android_arm32/libvoicechanger.so`：arm32 构建产物。

`packet/CMakeLists.txt` 的核心行为是：

- 将 `packet/world/src` 下的 WORLD 源码编译为静态库 `world`。
- 将 `packet/voicechanger/voicechanger.c` 编译为动态库 `voicechanger`。
- 链接 `world`、`log`、`m`，生成 Android `libvoicechanger.so`。

### World

`World` 是 WORLD 声码器源码目录，是 Android 主线算法依赖。

- `World/src/*.cpp`：WORLD 算法实现。
- `World/src/world/*.h`：WORLD 对外头文件。
- `World/README.md`：WORLD 项目自身说明。
- `World/src/world.lib`、`World/src/*.obj`：本地 Windows 编译产物，不是主线源码。

## 推荐维护入口

如果目标是 Android App 接入，优先阅读：

1. `app/include/voicechanger.h`
2. `app/voicechanger.c`
3. `packet/CMakeLists.txt`

如果目标是重新打 Android so，优先关注：

1. `packet/CMakeLists.txt`
2. `packet/voicechanger/voicechanger.c`
3. `packet/world/include` 和 `packet/world/src`
4. `packet/build_android_arm64`、`packet/build_android_arm32`

如果目标是调试算法效果，优先关注：

1. `app/voicechanger.c`
2. `packet/voicechanger/voicechanger.c`
3. `World/src`

## 当前对外接口和音频约定

当前 Android so 主线暴露的是 C 风格接口：

```c
int vc_init(void);
void vc_set_params(double pitch, double formant);
PCMFrame10ms vc_process(PCMFrame10ms input);
void vc_destroy(void);
```

核心音频约定：

- 采样率：`48000 Hz`
- 处理帧长：`10ms`
- 单帧采样数：`480`
- PCM 格式：`int16_t`
- 单帧结构：`PCMFrame10ms`，内部是 `int16_t data[480]`

典型调用顺序：

```text
vc_init()
vc_set_params(pitch, formant)
循环调用 vc_process(input_frame)
vc_destroy()
```

## archive 归档区

`archive` 保存历史实验工程和非 Android 主线依赖，便于根目录保持清爽。

- `archive/python-prototype`：Python + pyworld + sounddevice 的实时变声原型。
- `archive/windows-tests`：Windows + PortAudio 实时测试工程。
- `archive/sdk-experiments`：C/C++ SDK、C 接口、10ms/JNI 等历史实验版本。
- `archive/third-party/portaudio`：Windows 实时输入输出依赖库，Android so 主线不依赖它。

整理时删除了 Visual Studio `.vs` 本地缓存目录；这些目录不是源码，后续打开 Visual Studio 工程时会自动重新生成。

## GitHub 上传准备

根目录新增了 `.gitignore`，用于后续上传 GitHub 时排除 IDE 缓存、中间产物和常见构建输出。

保留为可追踪的 Android 交付产物：

- `app/libs/arm64-v8a/libvoicechanger.so`
- `app/libs/armabi-v7a/libvoicechanger.so`
- `packet/build_android_arm64/libvoicechanger.so`
- `packet/build_android_arm32/libvoicechanger.so`

后续上传前如果要把 `World` 和 `archive/third-party/portaudio` 作为普通源码目录提交，需要先处理它们内部的 `.git` 元数据，避免变成嵌套仓库或子模块。

