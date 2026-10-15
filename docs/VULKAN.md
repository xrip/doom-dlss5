# Vulkan and DLSS 5 neural rendering

The engine uses C23, Clang, Vulkan 1.3, and GLFW. Small C++17 adapters call
NGX. Rendering, presentation, conversion, movement, map interaction, and
WinMM music stay in C23. SPIR-V shaders are embedded in the executable.

## Run

The current local build includes neural rendering:

```powershell
./build/doom-raylib.exe ./build/MAP01.drmap --dlss5
./tools/run-map.ps1 -Wad ./DOOM2.WAD -Map MAP01 -Dlss5
```

DLSS starts enabled when built in, with style 1 and two NR passes.
FG starts disabled; ray lighting starts enabled. `--dlss` and
`--dlss5` are aliases. Use `--no-dlss`, `--no-fg`, and `--no-ray-lighting`
to start with effects off. The GPU resources and runtimes remain loaded
so effects can be switched back on without leaving the level.

- F5 switches DLSS (NR + SR) on/off, keeping the selected SR mode.
- F9 switches frame generation on/off, independently of NR and SR.
- L switches ray lighting on/off.
- F6 switches neural rendering on/off; SR stays active if selected.
- F7 cycles neural styles 0, 1, and 2.
- F8 cycles 1, 2, and 4 neural passes.
- `--nr-style 0|1|2` and `--nr-passes 1..4` set initial choices.
- `--sr off|quality|balanced|performance` selects Super Resolution.
- With `--dlss5`, SR defaults to performance; use `--sr off` for native-resolution NR.
- The HUD shows the active mode, style, and pass count.

Two passes are the default. More passes feed neural output through the model
again, with a separate feature/history for each pass. They increase GPU
work and can amplify both detail and artifacts. They do not replace the
level mesh or sprite assets. Native resolution is supported; Super
Resolution is not required.

```powershell
./build/doom-raylib.exe ./build/MAP01.drmap --dlss5 --nr-passes 4
```

Normal runs stay open until Esc or window close. Only `--frames N` ends
after a fixed frame count; screenshot-only runs default to two frames.
Do not expect every sprite to become a photoreal character. The close-up
captures show real changes to shading and detail while much of the
original pixel-art structure remains.

## Build

Install CMake 3.28+, Ninja, Clang 19+, the Windows SDK/build tools, and a
Vulkan SDK with `glslc`. A native-only build needs no NGX SDK:

Run CMake from an x64 Visual Studio developer shell, with Clang and Ninja
on `PATH` and `VULKAN_SDK` pointing to the installed Vulkan SDK.

```powershell
cmake --preset clang -DDOOM_DLSS5=OFF -DDOOM_FRAME_GENERATION=OFF
cmake --build --preset clang
```

For NR, use the public [NVIDIA/DLSS SDK](https://github.com/NVIDIA/DLSS).
The tested revision is `374959484e79a640feaba44c93ac8cfb0a03f5b5`.
A fresh small checkout can be made with:

```powershell
git clone --filter=blob:none --no-checkout https://github.com/NVIDIA/DLSS.git build/_deps/dlss-public-src
git -C build/_deps/dlss-public-src sparse-checkout set include lib/Windows_x86_64/x64 lib/Windows_x86_64/rel
git -C build/_deps/dlss-public-src checkout 374959484e79a640feaba44c93ac8cfb0a03f5b5
```

The neural runtime is separate from that SDK. The tested Blackwell DLL was
obtained from this [community-hosted 310.8.0 release](https://github.com/RankFTW/rhi-repo/releases/tag/dlssnr-310.8.0).
It is version `310.8.0.0`, 165840496 bytes, with a locally verified valid
NVIDIA Corporation Authenticode signature. Expected SHA-256:

```text
e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e
```

Keep the DLL locally as `build/runtime/nvngx_dlssnr.dll`. SDK binaries and
the model are not committed. Community source licenses do not cover the
NVIDIA runtime. Check a local copy with `Get-FileHash` and
`Get-AuthenticodeSignature` before using it.

```powershell
cmake --preset clang -DDOOM_DLSS5=ON -DDOOM_GPU_TESTS=ON `
    -DDLSS5_SDK="$PWD/build/_deps/dlss-public-src" `
    -DDLSS5_RUNTIME="$PWD/build/runtime/nvngx_dlssnr.dll"
cmake --build --preset clang
ctest --preset clang
```

The tested setup is Windows x64, RTX 5060 Ti 16 GB, driver 617.42, Vulkan
SDK 1.4.357.0, and Clang 23.1.1. Other GPU architectures and runtime
versions need separate validation.

## Runtime path

This uses the public NGX parameter API and the NR DLL's discovered Vulkan
exports, based on [bevy_dlss5's integration](https://github.com/AlrikOlson/bevy_dlss5/tree/28be5dc1b4b907ddba63a0c73d531e629809a865).
It is an experimental community ABI, not a beta-SDK integration.
No assigned NVIDIA application ID is needed: NGX core uses a stable custom
project GUID. The snippet's separate compatibility identifier follows the
tested community call sequence.

The tested runtime checks the process filename. CMake therefore makes an
executable copy named `nvngx.dll` beside `doom-raylib.exe`. This is our
engine executable, not an NVIDIA library. In a DLSS-enabled build,
the launcher starts that copy, preserves arguments/stdout/stderr, waits,
and returns its exit status. The runtime itself is `nvngx_dlssnr.dll`.
No NVIDIA DLL bytes or system drivers are patched.

The SR runtime `nvngx_dlss.dll` comes from the SDK's `lib/Windows_x86_64/rel`
folder and is copied by CMake. `DLSS5_SR_RUNTIME` can select another local copy.
The tested 310.9.1 runtime has SHA-256
`3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983`.

Keep all four files together when moving the NR build. CTest uses a
separate `nr-check/nvngx.dll` copy of the GPU test executable.

The DLL supplies its Vulkan extension requirements. On the tested system:
`VK_KHR_get_physical_device_properties2`, `VK_NVX_binary_import`,
`VK_NVX_image_view_handle`, `VK_KHR_buffer_device_address`, and
`VK_KHR_push_descriptor`. Buffer device addresses are enabled explicitly.
Feature creation/evaluation errors stop the run; there is no silent
fallback claiming NR is active.

## Frame contract

World color is display-encoded RGBA8, motion is RG16F, and depth is normal
non-inverted D32. The model runs before HUD rendering. Motion is current
minus previous normalized UV with positive Y downward; the bridge converts
it to previous-minus-current pixel displacement. With SR enabled, world
and NR use the SDK's recommended render size, followed by SR to display
size and then a full-resolution HUD. A 32-sample Halton sequence jitters
the world; motion includes jitter and SR uses the `MVJittered` flag.
With SR off there is no jitter or upscaling.
Camera cuts, teleports, resize, and mode/style/pass changes reset
history. Each repeated pass has its own temporal state.

The tested SR DLL issues image clears without a sufficient transfer-write
dependency. NGX Vulkan callbacks wrap SDK color clears with a memory barrier
from all prior writes to clear/transfer writes. This fixes the observed
write-after-write hazards without hiding validation errors or editing DLLs.
NGX core shutdown runs before the direct NR snippet shutdown to release
SDK-owned resources while its context is still valid.

Defaults are intensity 1, local tone 1, local structure 1.6, skin structure
-1 (follow local structure), and auto mask enabled. The unused
`GlobalToneStrength` key is not set. These are actual model controls;
there is no separate sharpening filter presented as neural enhancement.

## Checks and captures

GPU tests require the local WAD, a Vulkan device, desktop session, and
`VK_LAYER_KHRONOS_validation`. CTest enables synchronization validation.
Native tests cover palette preservation through presentation, billboard
orientation at eight angles, sky motion, sector movement, and resize.
NR tests also check changed image output, toggling, and feature history.

`--screenshot FILE.png` captures the presentation conversion.
`--motion FILE.pfm` captures actual GPU motion: red/green hold horizontal/
vertical normalized motion; blue is zero. PFM rows run bottom to top.
Readback happens only for captures or diagnostics.
`--profile` prints average frame and GPU stage times after 16 warm-up frames.
`--size WIDTH HEIGHT` sets the initial display size. Use a fixed `--frames`
count and run benchmarks without other GPU tests running. Frame generation
is separate from SR and neural rendering; see the optional integration below.
Do not combine timing runs with screenshot or motion captures: CPU readback
and PNG encoding are included in the elapsed frame time.
`renderer_pixels` returns borrowed RGBA storage valid until the next color
readback, resize, or renderer destruction.

Local comparison artifacts include `build/compare-neural.html`,
`build/monster-off.png`, and the style/pass captures next to it.
See [VALIDATION.md](VALIDATION.md) for evidence and limitations.
Ray lighting is optional; see its controls and limits below.

## Frame generation

Frame generation now uses the public NGX API and our own C23 Vulkan
presentation worker. Streamline is no longer linked or required.

After configuring the NR build above, enable the optional FG adapter:

```powershell
cmake --preset clang -DDOOM_FRAME_GENERATION=ON
cmake --build --preset clang
./build/doom-raylib.exe ./build/MAP01.drmap --dlss5 --fg
./tools/run-map.ps1 -Wad ./DOOM2.WAD -Map MAP01 -FrameGeneration
```

In this PoC, FG requires a DLSS-enabled build so the features share one NGX
context. F5 can disable NR and SR while FG stays active.
The run-map switch also enables `--dlss5`. CMake copies the SDK's signed
`nvngx_dlssg.dll` beside the executable; `DLSS5_FG_RUNTIME` can select
another local copy. Keep this fifth file with the four NR/SR files.
The tested runtime comes from the same pinned public SDK revision.

The engine presents one interpolated frame followed by the real frame.
Reset frames and SDK-requested interpolation skips present only the real
frame. The worker uses a high-resolution Windows timer and a separate
Vulkan queue; it does not sleep on the render thread. Acquire and present
calls share a lock to meet Vulkan's swapchain host-access rules. Resize
drains the worker before replacing the swapchain and resets model history.

The HUD explicitly shows **render FPS** and **FG 2x** when enabled. Exit
logs count successful real/generated presentation calls, not rendered FPS
multiplied by two. These counts are not a measurement of monitor scanout.
`--frames N` still counts application-rendered frames. `GPU-post` includes
HUD and FG evaluation; the extra presentation-copy submission is not timed
by that GPU query range.

The tested NGX FG runtime needs two integration workarounds through its
Vulkan callbacks: transfer-write dependencies before SDK clears/fills,
and an initial GENERAL layout for newly bound FG images that the SDK has
not transitioned. Image tracking is limited to feature creation. The
isolated test produced the same four output hashes before and after these
fixes, while validation errors fell to zero. No NVIDIA DLL or driver is
patched, and no validation errors are filtered.

`renderer_generated_pixels` exposes the last valid interpolated image for
tests. It returns NULL for reset/skipped frames and uses the same borrowed
readback storage as `renderer_pixels`.

The earlier Streamline trial and its SDK-owned validation failures are
recorded in [VALIDATION.md](VALIDATION.md). Direct FG does not use
Streamline Reflex or Multi Frame Generation. Other GPU/runtime versions
still need their own validation.

## Ray-traced direct lighting

Use `--ray-lighting`; `L` switches between ray-lit and classic output.
It works alone or before NR, SR, and FG:

```powershell
./build/doom-raylib.exe ./build/MAP01.drmap --ray-lighting --no-vsync
./build/doom-raylib.exe ./build/MAP01.drmap --dlss5 --fg --ray-lighting
./tools/run-map.ps1 -Wad ./DOOM2.WAD -Map MAP01 -FrameGeneration -RayLighting
```

The renderer uses Vulkan ray queries, a world BLAS and one-instance TLAS.
Doors, floors, and other world movers refit those structures when vertex
positions change. Texture/UV data stays current for scrolling and switches.
Shadow rays test texture alpha, so grates do not cast solid rectangular
shadows. Sky surfaces do not block the sun. Fullbright sprites stay bright.

Lighting uses a fixed sun and up to four nearby ceiling lights derived
from sector light levels and floor triangles. These are inferred sources,
not authored Doom light fixtures. Ceiling sources emit downward; part of
the original sector shading remains as ambient light. Ray lighting starts
enabled; turning it off restores the classic palette path.

This is direct diffuse lighting with hard shadows, not a path tracer,
global illumination, ray reconstruction, or neural radiance caching.
World geometry casts shadows; billboard sprites currently receive lighting
but do not cast shadows. Nearest-light selection can change as the camera
moves. There is no player flashlight in this stage.

Required features include ray query, acceleration structures, buffer device
address, shader int64, runtime descriptor arrays, and nonuniform sampled
image indexing. The RTX 5060 Ti reports all of them. Ray resources are not
created unless requested through `RendererOptions`. The interactive viewer
keeps them ready for live switching, including when started with
`--no-ray-lighting`. This stage adds no lighting data to the scene format.

`--no-vsync` requests immediate presentation for native/SR-only runs; FG
already uses it with its own pacing. The normal native path uses FIFO.
Use uncapped runs when measuring shader cost separately from desktop
presentation limits. The `GPU-world` profile stage includes ray work.
