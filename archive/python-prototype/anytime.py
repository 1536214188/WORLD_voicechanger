import numpy as np
import pyworld as pw
import sounddevice as sd
import threading
import queue
import time
import sys

# ===============================
# 1. 实时流式参数设置
# ===============================
fs = 48000
pitch_scale = 1.6  # 女声转男声 (0.7)1.6
formant_shift = 0.8  # 共振峰下移 (1.2)0.8

# 为了保证实时性，减小分块大小
hop_duration = 0.1  # 每次步进 0.2 秒 (决定了最低延迟)
overlap_duration = 0.01  # 块与块重叠 0.05 秒 (用于消除接缝爆音)

hop_samples = int(fs * hop_duration)
overlap_samples = int(fs * overlap_duration)
chunk_samples = hop_samples + overlap_samples

# 音频通讯双向队列
in_queue = queue.Queue()
out_queue = queue.Queue()


# ===============================
# 2. 核心变声线程 (Worker)
# ===============================
def process_audio():
    print("[变声引擎] 启动成功，正在后台实时运算...")

    # 初始化状态缓冲区
    input_buffer = np.zeros(chunk_samples)
    prev_overlap = np.zeros(overlap_samples)

    # 预计算交叉淡入淡出窗
    fade_in = np.linspace(0, 1, overlap_samples)
    fade_out = np.linspace(1, 0, overlap_samples)

    # 预充填一些空白数据到输出队列，防止最开始时扬声器拿不到数据卡顿 (Buffer)
    for _ in range(2):
        out_queue.put(np.zeros(hop_samples))

    # 初始化计时相关变量
    process_count = 0
    total_world_time = 0.0
    start_time = time.time()

    while True:
        # 从麦克风队列获取最新声音
        new_data = in_queue.get()
        if new_data is None:  # 收到退出信号
            # 打印最终的WORLD处理统计
            if process_count > 0:
                avg_world_time = total_world_time / process_count
                print(f"[WORLD统计] 处理了 {process_count} 个音频块，平均处理时间: {avg_world_time * 1000:.2f}ms")
            break

        # 维护滑动窗口
        input_buffer = np.roll(input_buffer, -hop_samples)
        input_buffer[-hop_samples:] = new_data

        # 静音检测，减小无声音时的CPU压力
        if np.max(np.abs(input_buffer)) < 0.005:
            Y = np.zeros(chunk_samples)
        else:
            # 记录WORLD处理开始时间
            world_start = time.perf_counter()

            # WORLD 分析 (必须使用极速版 dio)
            _f0, t = pw.dio(input_buffer, fs)
            f0 = pw.stonemask(input_buffer, _f0, t, fs)
            sp = pw.cheaptrick(input_buffer, f0, t, fs)
            ap = pw.d4c(input_buffer, f0, t, fs)

            # 变调
            f0_new = f0.copy()
            f0_new[f0 > 0] *= pitch_scale

            # 变共振峰 (向量化)
            num_bins = sp.shape[1]
            sp_new = np.zeros_like(sp)
            source_indices = (np.arange(num_bins) * formant_shift).astype(int)
            valid_mask = source_indices < num_bins
            sp_new[:, valid_mask] = sp[:, source_indices[valid_mask]]
            sp_new[:, ~valid_mask] = np.expand_dims(sp[:, -1], axis=1)

            # 重合成
            Y = pw.synthesize(f0_new, sp_new, ap, fs)

            # 计算WORLD处理耗时
            world_time = time.perf_counter() - world_start
            total_world_time += world_time
            process_count += 1

            # 每处理10个音频块打印一次平均处理时间
            if process_count % 10 == 0:
                avg_world_time = total_world_time / process_count
                print(
                    f"[WORLD处理] 块{process_count}: 本次处理 {world_time * 1000:.2f}ms, 平均 {avg_world_time * 1000:.2f}ms, 实时性: {world_time / hop_duration * 100:.1f}%")

        # 强制修正因浮点误差导致的长度偏差
        if len(Y) > chunk_samples:
            Y = Y[:chunk_samples]
        elif len(Y) < chunk_samples:
            Y = np.pad(Y, (0, chunk_samples - len(Y)))

        # 交叉平滑重叠相加 (Overlap-Add) -> 消除 bibobibo 爆音
        blended = prev_overlap * fade_out + Y[:overlap_samples] * fade_in
        output_chunk = np.concatenate([blended, Y[overlap_samples:hop_samples]])

        # 保存尾部供下次平滑
        prev_overlap = Y[hop_samples:]

        # 将处理好的声音推给扬声器队列
        out_queue.put(output_chunk)


# 启动处理线程
processor_thread = threading.Thread(target=process_audio)
processor_thread.start()


# ===============================
# 3. 音频底层 I/O 回调函数
# ===============================
def audio_callback(indata, outdata, frames, time_info, status):
    """这个函数会由声卡硬件定时严格调用（每 0.2 秒一次）"""
    if status:
        print(status, file=sys.stderr)

    # 1. 把麦克风录到的声音扔给处理线程
    # indata 的形状是 (frames, channels)
    in_queue.put(indata[:, 0].copy())

    # 2. 尝试从处理线程拿转换好的声音，喂给扬声器
    try:
        out_chunk = out_queue.get_nowait()
        outdata[:, 0] = out_chunk
    except queue.Empty:
        # 如果 CPU 处理太慢没跟上，就输出静音，防止程序崩溃
        outdata.fill(0)


# ===============================
# 4. 开启麦克风与扬声器流
# ===============================
print("\n" + "=" * 50)
print("🎙️  实时变声器已就绪！")
print("⚠️  警告：请务必戴上耳机！否则会产生极高分贝的刺耳啸叫！")
print("🔴  对着麦克风说话即可听见变声，按 [Ctrl + C] 结束运行。")
print("=" * 50 + "\n")

try:
    # 开启全双工流：同时使用麦克风(channels=1)和扬声器(channels=1)
    with sd.Stream(samplerate=fs,
                   blocksize=hop_samples,
                   channels=1,
                   callback=audio_callback):
        # 保持主线程存活
        while True:
            time.sleep(0.1)
except KeyboardInterrupt:
    print("\n🛑 收到停止信号，正在关闭流...")
except Exception as e:
    print(f"\n❌ 发生音频设备错误: {e}")
finally:
    # 发送结束信号并清理线程
    in_queue.put(None)
    processor_thread.join()
    print("✅ 变声器已安全退出。")