voice_c_dynamic_block

Purpose:
- Variable block-size C API experiment.
- Keeps vc_init(int sample_rate).
- Keeps struct-by-value vc_process(PCMFrame10ms input).
- Adds PCMFrame10ms.samples so callers can pass 960, 1024, 320, etc.

Rules:
- input.samples must be > 0 and <= VC_FRAME_SAMPLES.
- vc_process() returns output.samples == input.samples.
- WORLD still uses the real sample_rate and internal FIFO buffering.

Windows build:
1. Open Visual Studio Developer Command Prompt.
2. cd /d D:\voice_project\archive\sdk-experiments\voice_c_dynamic_block
3. build.bat
