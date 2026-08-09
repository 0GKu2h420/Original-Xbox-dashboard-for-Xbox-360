Original Xbox Custom Dashboard
An open-source custom dashboard and application for JTAG/RGH-modified Xbox 360 consoles, built using C++ and Direct3D 9. It features a fully rendered 3D background mesh, dynamic particle systems, ambient audio management via XAudio2, and an authentic multi-layered menu state machine mirroring classic console interfaces.

Features
D3D9 Graphics Pipeline: Custom vertex and pixel shaders with real-time 3D rotation matrix transformations.

XAudio2 Sound Engine: Dynamic playback for ambient environments (steam vents, voice chatter, track loops) and interactive UI navigation cues with 16-bit audio byte swapping.

Interactive Navigation: Fully responsive controller input handling via XInput with debounce timers and smooth menu transitions.

Asset Streaming: On-the-fly texture extraction parsing raw DDS streams directly from binary containers.

File Structure & Requirements
To run this executable on a modified Xbox 360 (RGH/JTAG), ensure your storage device (USB or internal HDD) contains the compiled .xex alongside the required media and audio assets:

Plaintext
usb:\default.xex
usb:\000900\Media\XIP\mainmenu5.xip.d\decompressed_stream.bin
usb:\Audio\
    ├── MainAudio\
    ├── AmbientAudio\
    ├── TransitionAudio\
    ├── MemoryAudio\
    ├── MusicAudio\
    └── SettingsAudio\
Building from Source
Open the solution in Visual Studio configured for the Xbox 360 SDK target platform.

Ensure your additional library directories point to your XEDK path:

Plaintext
$(XEDK)\lib\xbox
Set your Linker's Additional Dependencies to include:

Plaintext
xapilib.lib
d3d9.lib
d3dx9.lib
xgraphics.lib
xboxkrnl.lib
xnet.lib
Rebuild the solution for the Xbox 360 Release configuration.
