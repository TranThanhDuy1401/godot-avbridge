# FIXES
## 1. Forward+ push constant mismatch
**Problem:**
Video playback failed on Godot Forward+ due to a push constant
size mismatch in the NV12 GPU converter.

**Fix:**
Updated the push constant size in `nv12_gpu_converter.cpp` to match
the shader layout.

**Result:**  
Forward+ and Mobile renderers video playback works correctly

## 2. No audio of video when using MediaFoundation (Windows)

**Problem:**  
Media Foundation stream selection could fail when using certain
audio or video stream indices.

**Fix:**  
Corrected the stream index validation in
`avb_decoder_mediafoundation.cpp`.

**Result:**  
Audio and video streams are selected correctly when using explicit
stream indices.
