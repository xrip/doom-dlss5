# Implementation sources

The project uses public format descriptions and library APIs. No Doom
engine source or renderer port source was used to write this PoC.

- [Kaitai Doom WAD format](https://github.com/kaitai-io/kaitai_struct_formats/blob/master/game/doom_wad.ksy): WAD directory, classic map records, PNAMES, and texture descriptions.
- [Doom Wiki picture format](https://doomwiki.org/wiki/Picture_format): column posts, patch offsets, and gaps.
- [Doom Wiki sidedef](https://doomwiki.org/wiki/Sidedef): texture offsets and surface types.
- [Doom Wiki linedef](https://doomwiki.org/wiki/Linedef): sidedef links and pegging flags.
- [Doom Wiki COLORMAP](https://doomwiki.org/wiki/COLORMAP): palette-index light tables.
- [Doom Wiki sky](https://doomwiki.org/wiki/Sky): F_SKY1 and sky boundaries.
- [raylib 6.0 API](https://github.com/raysan5/raylib/blob/6.0/src/raylib.h): meshes, textures, shaders, camera, window, and screenshots.
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

Dependencies are pinned in CMake: raylib tag 6.0 at
`dbc56a87da87d973a9c5baa4e7438a9d20121d28`, and libtess2 at
`8dbd6483e920311a58c9af10a10beb278efebc36`. Their license files remain
in the downloaded source trees under `build/_deps`.
