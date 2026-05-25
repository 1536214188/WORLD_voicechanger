VoiceChangerProject5

Purpose:
- Windows PortAudio realtime test for voice_c_dynamic_block.
- Tests struct-based variable block sizes without pointer public API.

Build:
1. Open Visual Studio Developer Command Prompt.
2. cd /d D:\voice_project\archive\windows-tests\VoiceChangerProject5
3. cmake -S . -B build
4. cmake --build build --config Release

Run:
1. cd /d D:\voice_project\archive\windows-tests\VoiceChangerProject5\build\Release
2. main.exe
3. Try 48000 + 960, 48000 + 1024, or 16000 + 320.
