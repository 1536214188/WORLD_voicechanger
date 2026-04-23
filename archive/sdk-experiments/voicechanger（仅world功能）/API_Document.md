
### 详细使用文档 (`API_Document.md`)

```markdown
# VoiceChanger SDK 使用指南

## 2. 核心函数接口

### 2.1 初始化引擎
```c
int vc_init();
```
*   **功能**: 初始化内存缓存区，预分配 WORLD 算法所需的中间运算数组。
*   **注意**: 必须在第一次变声前调用。

### 2.2 变声处理 (核心接口)
```c
int vc_process_pcm(const int16_t* input_pcm, int samples, int16_t* output_pcm, double pitch, double formant);
```
*   **input_pcm**: 输入的 PCM 数据。
*   **samples**: 处理的音频采样点数 (建议为 1024 或 2048)。
*   **output_pcm**: 输出处理后的 PCM 数据。
*   **pitch**: 音高缩放倍率。
    *   `0.7` ~ `0.9` 变粗（男声）。
   
*   **formant**: 共振峰缩放倍率（调节音色）。
    *   `1.2`(女变男)

### 2.3 释放资源
```c
void vc_destroy();
```


