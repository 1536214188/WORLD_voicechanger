VoiceChangerProject4

Purpose:
- Windows PortAudio realtime test for voice_c_dynamic_sampling_min_api.
- Tests dynamic vc_init(sample_rate) with the current VC_FRAME_MS from include/voicechanger.h.

How to build:
1. Open Visual Studio Developer Command Prompt.
2. cd /d D:\voice_project\archive\windows-tests\VoiceChangerProject4
3. cmake -S . -B build
4. cmake --build build --config Release

How to run:
1. Copy release_package\portaudio.dll to build\Release if CMake does not do it automatically.
2. Run build\Release\main.exe.
3. Enter 48000 or 16000 when prompted.

Notes:
- Current header uses VC_FRAME_MS=20.
- 48000 Hz expects 960 samples per callback.
- 16000 Hz expects 320 samples per callback.
