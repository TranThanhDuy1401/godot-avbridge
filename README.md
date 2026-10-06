# godot-avbridge

Godot video/audio playback addon via GDExtension, powered by
[libavbridge](https://github.com/buresu/libavbridge).

libavbridge wraps the platform-native media stack (Media Foundation on Windows,
AVFoundation on macOS, GStreamer on Linux), so this addon plays common formats (H.264/HEVC/VP9 video, AAC/Opus audio in mp4/mov/webm/mkv) without bundling codec libraries.

## Usage

```gdscript
var stream := VideoStreamAVBridge.new()
stream.file = "res://video.mp4"   # or an absolute / user:// path
$VideoStreamPlayer.stream = stream
$VideoStreamPlayer.play()
```

With a RenderingDevice renderer, `VideoStreamAVBridge` requests NV12 frames,
uploads separate Y and UV textures, and converts them to RGBA with a compute
shader. Hardware decoding is preferred when the selected libavbridge backend
supports it. The current path still reads NV12 back to CPU memory before the
Godot texture upload; platform-native zero-copy import is planned separately.

Renderers without RenderingDevice support fall back to CPU-backed `RGBA8`.
Decoded audio is fed to the `VideoStreamPlayer` automatically.

## Build

```
git clone --recursive https://github.com/TranThanhDuy1401/godot-avbridge
cd godot-avbridge
mkdir build && cd build

# Linux / macOS
cmake -DCMAKE_BUILD_TYPE=[Debug|Release] ..
cmake --build . --target install

# Windows
cmake -G "Visual Studio 18 2026" ..
cmake --build . --config [Debug|Release] --target install
```

If you encounters error when compiling Release-Windows, use this instead:
```
cmake -G "Visual Studio 18 2026" .. -A x64 -DCMAKE_BUILD_TYPE=Release
```

The build links libavbridge statically into a single shared object. The
GStreamer runtime libraries are loaded dynamically at play time and
must be installed on the target system (see the libavbridge README).

## Runtime dependencies (Linux)

GStreamer 1.x with the `base` and `good` plugin sets.  
Additional plugins are supported only if they and their dependencies are LGPL-compatible. GPL-licensed and non-free GStreamer plugins are not supported.  

## Windows Notice:
Vorbis audio codec tested, it's not working. If you encounter the same issue, it is recommended to change the audio codec to Opus.

## License

MIT License
