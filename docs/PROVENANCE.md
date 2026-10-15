# Implementation sources

The project uses public format descriptions and library APIs. No Doom
engine or renderer port code is included in this PoC. The optional voxel
format and definition behavior were checked against the references below;
the converter and mesher are independently written.

- [Ken Silverman's SLAB6 format notes](https://www.advsys.net/ken/slab6.zip): KVX mip layout, offsets, pivots, surface runs, and palettes.
- [GZDoom voxel definitions](https://github.com/ZDoom/gzdoom/blob/master/src/r_data/voxeldef.cpp): definition keys and angle/spin conventions.
- [GZDoom voxel format reference](https://github.com/ZDoom/gzdoom/blob/master/src/common/models/models_voxel.cpp): exposed-face directions and pivot/axis convention, checked as format behavior; no implementation code was copied.
- [GZDoom model coordinate convention](https://github.com/ZDoom/gzdoom/blob/master/src/r_data/models.cpp): actor yaw and local-angle composition, used to check the conversion to this project's negative-map-Y world axis.

- [Kaitai Doom WAD format](https://github.com/kaitai-io/kaitai_struct_formats/blob/master/game/doom_wad.ksy): WAD directory, classic map records, PNAMES, and texture descriptions.
- [Doom Wiki picture format](https://doomwiki.org/wiki/Picture_format): column posts, patch offsets, and gaps.
- [WhackEd4 Doom 1.9 editor tables](https://github.com/GitExl/WhackEd4/blob/ebe673c4ef5c98b6e68f5d2dc703aee542dc795f/cfg/tables_doom19.json): factual map-thing spawn frame sequences, tic durations, and fullbright bits. Only editor data was consulted; playback is independently written.
- [Doom Wiki sidedef](https://doomwiki.org/wiki/Sidedef): texture offsets and surface types.
- [Doom Wiki linedef](https://doomwiki.org/wiki/Linedef): sidedef links and pegging flags.
- [Doom Wiki COLORMAP](https://doomwiki.org/wiki/COLORMAP): palette-index light tables.
- [Doom Wiki sky](https://doomwiki.org/wiki/Sky): F_SKY1 and sky boundaries.
- [raylib 6.0 API](https://github.com/raysan5/raylib/blob/6.0/src/raylib.h): the original renderer, replaced by the native Vulkan renderer.
- [Vulkan specification](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html): GPU resources, rendering, and synchronization.
- [GLFW](https://www.glfw.org/docs/latest/): windows, input, and Vulkan surfaces.
- [stb](https://github.com/nothings/stb): PNG screenshots and HUD text.
- [NVIDIA public NGX SDK](https://github.com/NVIDIA/DLSS/tree/374959484e79a640feaba44c93ac8cfb0a03f5b5): Vulkan resources, parameter API, and custom project identification.
- [NVIDIA RR integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md): reconstruction inputs, linear depth, albedos, normals, and output resolution.
- [NVIDIA FG integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md): final-resolution color/UI inputs, render-resolution depth/motion, and UI recomposition.
- [bevy_dlss5](https://github.com/AlrikOlson/bevy_dlss5/tree/28be5dc1b4b907ddba63a0c73d531e629809a865): discovered NR Vulkan exports, runtime initialization, and parameter keys. The C++ adapter is independently written against that call contract.
- [Measured NR controls](https://github.com/Echo-Storm/ls-addon-manager/blob/main/addons/DLSS5NR01/docs/dlssnr-knobs.md): observed style, structure, mask, and repeated-pass behavior for runtime 310.8.
- [libtess2](https://github.com/memononen/libtess2): sector contour tessellation, used only by the converter.
- [The Unofficial Doom Specs, chapter 4](https://github.com/nukeop/TheUnofficialDoomSpecs/blob/master/Chapter4.md): player dimensions, floor step limit, linedef flags, sector IDs, specials, and tags.
- [Doom Builder guide](https://www.doombuilder.com/files/doombuilder1guide.pdf): door actions and key colors.
- [Boom reference](https://raw.githubusercontent.com/fabiangreffrath/woof/master/docs/boomref.txt): public tables of classic floor, stair, platform, crusher, light, teleport, scrolling, exit, and sector actions. Only the documentation was used, not port implementation source.
- [Movement comparison](https://github.com/0dot0repeating/quakemovement#useful-info-i-guess): documented acceleration, friction, terminal speeds, and their formula; no movement engine source was copied.
- [WinMM MCI](https://learn.microsoft.com/en-us/windows/win32/multimedia/mci): native Windows MIDI sequencer commands.

The local WAD was inspected for original map record counts, sector contours,
thing types, and referenced assets. It is not copied into source files.
Tests generate their own room, palette, textures, and malformed inputs.
The GPU scene format, sector mesh code, light-row choice, and sky shader
are project designs. Visual similarity does not mean exact original
render behavior. This note records sources; it is not a claim of a
formally separated clean-room process.

Direct lighting uses Vulkan ray queries as described by the
[Khronos GLSL ray-query extension](https://github.khronos.org/Vulkan-Site/glslext/latest/glslext/ext/GLSL_EXT_ray_query.html).
The lights are project-derived approximations of Doom sector lighting,
not copied authored light entities or an original Doom lighting algorithm.

Dependencies are pinned in CMake: GLFW 3.4 at
`a74efa0d5628b74adc0426af4c5710e287fa7c2c`, stb at
`2c980bb59875b0d32144a71867fbdebb2f77cd20`, and libtess2 at
`8dbd6483e920311a58c9af10a10beb278efebc36`. Their license files remain
in the downloaded source trees under `build/_deps`.
