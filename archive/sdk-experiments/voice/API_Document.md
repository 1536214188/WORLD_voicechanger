
##  架构与数据流 (Architecture & Data Flow)

由于 WORLD 算法的特性，仅处理 20ms 的音频会导致提取的基频（F0）和频谱严重失真。本 SDK 内部实现了一个**黑盒式缓冲引擎**来解决这一矛盾：

* **音频格式**：采样率 `48000 Hz`，单声道，16-bit PCM。
* **外部交互帧 (Frame)**：`20ms` (`960 samples`)
* **内部处理块 (Block)**：`200ms` (`9600 samples`)

**工作原理 (黑盒机制)：**
1. **输入阶段**：外部系统每次调用 `process()` 传入 `20ms` 数据，SDK 会将其推入内部的输入队列 (`in_fifo`)。
2. **积攒阶段**：在最开始的几次调用中，内部未凑齐处理所需的 `200ms` 长度，此时 `process()` 会立刻返回 `20ms` 的静音数据（即不可避免的算法初始延迟）。
3. **处理阶段**：当内部凑齐 `200ms` 时，触发 WORLD 算法进行集中计算。
4. **缝合阶段**：WORLD 计算产出的 `200ms` 音频中，前 `20ms` 会与上一次历史输出的尾部进行平滑融合（Crossfade），然后存入输出队列 (`out_fifo`)。
5. **输出阶段**：无论内部是否触发了集中计算，`process()` 始终会从 `out_fifo` 头部弹出 `20ms` 处理好的变声数据返回给调用方。

---

## 3. API 接口参考 (API Reference)

所有的类和方法都封装在 `VoiceChanger` 类中。

### 3.1 构造与初始化
```cpp
VoiceChanger();
~VoiceChanger();

bool init();
```
* **说明**：`init()` 用于为内部 FIFO 队列和历史重叠区预分配内存。
* **前置条件**：在开始推送任何音频流之前，**必须**调用一次 `init()`。
* **返回值**：成功返回 `true`。

### 3.2 参数设置
```cpp
void setParams(double pitch_scale, double formant_shift);
```
* **说明**：实时动态调节变声参数。本函数非常轻量，可在音频处理循环中随时调用。
* **参数**：
  * `pitch_scale` (音高倍率)：控制声音的高低。`> 1.0` 为变高（如：转女声、童声），`< 1.0` 为变低（如：转男声、大叔），`1.0` 为原声。推荐范围 `0.5 ~ 2.0`。
  * `formant_shift` (共振峰倍率)：控制声道特征（音色）。通常建议与 `pitch_scale` 保持相近的值，以获得自然的效果。

### 3.3 核心处理 (流式变声)
```cpp
std::vector<int16_t> process(const std::vector<int16_t>& input_pcm);
```
* **说明**：变声器的心脏函数。接收一段定长的 PCM 数据，返回等长的变声后 PCM 数据。
* **参数**：`input_pcm`。**极其重要：该容器的 `.size()` 必须严格等于 `960`（即 48kHz 下的 20ms）。**
* **返回值**：变声后的音频流。其长度**必定**为 `960`。


### 3.4 释放与销毁
```cpp
void destroy();
```
* **说明**：清空内部所有的缓存队列与历史平滑状态。
* **使用时机**：当用户挂断语音、停止变声功能，或销毁该 C++ 对象前调用，以释放内存占用并重置引擎状态。

---

## 4. 业务端集成指南 (Integration Guide)

以下是一个通用的伪代码/标准 C++ 示例，展示如何在你的业务逻辑（例如 Android JNI 封装层或自定义的音频流引擎）中使用本 SDK：

```cpp
#include "voicechanger.h"

class MyAudioEngine {
private:
    VoiceChanger vc;
    bool is_running = false;

public:
    void startVoiceChanger() {
        // 1. 初始化引擎
        if (!vc.init()) {
            // 处理初始化失败逻辑
            return;
        }

        // 2. 设置为目标音色 
        vc.setParams(0.7, 1.2);
        is_running = true;
    }

    void stopVoiceChanger() {
        is_running = false;
        // 4. 清理内部状态
        vc.destroy();
    }

    // 假设这是你的底层音频系统每隔 20ms 触发一次的数据回调
    // 或者是你在某个独立线程中不断从网络拉取音频包的循环
    void onReceiveAudioData20ms(const std::vector<int16_t>& raw_microphone_data) {
        if (!is_running) return;

        // 验证输入长度 (48000Hz * 0.02s = 960)
        if (raw_microphone_data.size() != 960) {
            return; // 拒绝非 20ms 标准的数据块
        }

        // 3. 傻瓜式调用：送入 20ms 原声，立刻获取 20ms 变声后数据
        // 业务层完全不需要关心底层的 200ms 和 Crossfade 逻辑
        std::vector<int16_t> processed_data = vc.process(raw_microphone_data);

        // 将变声后的数据发送给播放器、或通过网络发送 (如 WebRTC)
        sendToSpeakerOrNetwork(processed_data);
    }
};
```

---
