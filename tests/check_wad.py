import pathlib
import struct
import subprocess
import sys
import tempfile


def name(text):
    return text.encode("ascii").ljust(8, b"\0")


def make_wad(lumps):
    data = bytearray(b"IWAD" + b"\0" * 8)
    entries = []
    for label, contents in lumps:
        entries.append((len(data), len(contents), label))
        data.extend(contents)
    directory = len(data)
    for offset, size, label in entries:
        data.extend(struct.pack("<II8s", offset, size, name(label)))
    struct.pack_into("<II", data, 4, len(entries), directory)
    return bytes(data)


def room():
    vertices = [(0, 0), (0, 128), (128, 128), (128, 0)]
    patch = struct.pack("<HHhhII", 2, 2, 0, 2, 16, 23)
    patch += bytes([0, 2, 0, 0, 7, 0, 255, 1, 1, 0, 13, 0, 255])
    texture = name("WALL") + struct.pack("<IHHIH", 0, 2, 2, 0, 1)
    texture += struct.pack("<hhHHH", 0, 0, 0, 0, 0)
    return [
        ("MAP01", b""),
        ("THINGS", struct.pack("<hhHHH", 64, 64, 90, 1, 7)),
        ("LINEDEFS", b"".join(struct.pack("<7H", index, (index + 1) % 4, 0, 0, 0, index, 65535) for index in range(4))),
        ("SIDEDEFS", (struct.pack("<hh8s8s8sH", 0, 0, name("-"), name("-"), name("wall"), 0)) * 4),
        ("VERTEXES", b"".join(struct.pack("<hh", *point) for point in vertices)),
        ("SEGS", b"".join(struct.pack("<6H", index, (index + 1) % 4, 0, index, 0, 0) for index in range(4))),
        ("SSECTORS", struct.pack("<HH", 4, 0)),
        ("NODES", b""),
        ("SECTORS", struct.pack("<hh8s8shHH", 0, 128, name("FLAT"), name("FLAT"), 160, 0, 0)),
        ("REJECT", b"\0"),
        ("BLOCKMAP", b""),
        ("PLAYPAL", bytes(component for index in range(256) for component in (index, index, index))),
        ("COLORMAP", bytes(range(256)) * 32),
        ("PNAMES", struct.pack("<I8s", 1, name("PATCH"))),
        ("TEXTURE1", struct.pack("<II", 1, 8) + texture),
        ("PATCH", patch),
        ("FLAT", bytes(range(256)) * 16),
    ]


def run(arguments, good=True):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
    if good and result.returncode != 0:
        raise AssertionError(result.stdout + result.stderr)
    if not good and (result.returncode == 0 or "Error:" not in result.stderr):
        raise AssertionError(f"Expected a clean error: {result.returncode}: {result.stdout} {result.stderr}")
    return result


def outline_room(contours):
    vertices = []
    edges = []
    for contour in contours:
        first = len(vertices)
        vertices.extend(contour)
        edges.extend((first + index, first + (index + 1) % len(contour)) for index in range(len(contour)))
    replacements = {
        "VERTEXES": b"".join(struct.pack("<hh", *point) for point in vertices),
        "LINEDEFS": b"".join(struct.pack("<7H", start, end, 0, 0, 0, index, 65535) for index, (start, end) in enumerate(edges)),
        "SIDEDEFS": struct.pack("<hh8s8s8sH", 0, 0, name("-"), name("-"), name("WALL"), 0) * len(edges),
        "SEGS": b"".join(struct.pack("<6H", start, end, 0, index, 0, 0) for index, (start, end) in enumerate(edges)),
        "SSECTORS": struct.pack("<HH", len(edges), 0),
        "THINGS": struct.pack("<hhHHH", 16, 16, 90, 1, 7),
    }
    return [(key, replacements.get(key, contents)) for key, contents in room()]


def door_room():
    vertices = [(0, 0), (0, 128), (128, 128), (128, 0), (256, 128), (256, 0)]
    edges = [(0, 1, 0), (1, 2, 0), (2, 4, 1), (4, 5, 1), (5, 3, 1), (3, 0, 0)]
    lines = b"".join(struct.pack("<7H", start, end, 0, 0, 0, index, 65535) for index, (start, end, sector) in enumerate(edges))
    lines += struct.pack("<7H", 2, 3, 4, 1, 7, 6, 7)
    sides = b"".join(struct.pack("<hh8s8s8sH", 0, 0, name("-"), name("-"), name("WALL"), sector) for start, end, sector in edges)
    sides += b"".join(struct.pack("<hh8s8s8sH", 0, 0, name("WALL"), name("WALL"), name("-"), sector) for sector in (0, 1))
    segs = [(0, 1, 0, 0), (1, 2, 1, 0), (2, 3, 6, 0), (3, 0, 5, 0),
            (2, 4, 2, 0), (4, 5, 3, 0), (5, 3, 4, 0), (3, 2, 6, 1)]
    replacements = {
        "VERTEXES": b"".join(struct.pack("<hh", *point) for point in vertices),
        "LINEDEFS": lines,
        "SIDEDEFS": sides,
        "SEGS": b"".join(struct.pack("<6H", start, end, 0, line, side, 0) for start, end, line, side in segs),
        "SSECTORS": struct.pack("<4H", 4, 0, 4, 4),
        "NODES": struct.pack("<12h2H", 128, 0, 0, 128, *([0] * 8), 0x8001, 0x8000),
        "SECTORS": struct.pack("<hh8s8shHH", 0, 128, name("FLAT"), name("FLAT"), 160, 0, 0) +
                   struct.pack("<hh8s8shHH", 0, 0, name("FLAT"), name("FLAT"), 160, 0, 7),
    }
    return [(key, replacements.get(key, contents)) for key, contents in room()]


def collision_start(data):
    assets, batches, sprites = struct.unpack_from("<3I", data, 16)
    offset = 44 + 32768
    for _ in range(assets):
        width, height = struct.unpack_from("<II", data, offset + 8)
        offset += 24 + width * height * 4
    for _ in range(batches):
        count = struct.unpack_from("<I", data, offset + 12)[0]
        offset += 24 + count * 20
    return offset + sprites * 20


def main():
    converter, viewer = sys.argv[1:]
    checks = 0
    with tempfile.TemporaryDirectory(prefix="doom-raylib-test-") as temporary:
        root = pathlib.Path(temporary)
        wad, scene = root / "room.wad", root / "room.drmap"
        lumps = room()
        good_wad = make_wad(lumps)
        wad.write_bytes(good_wad)
        result = run([converter, str(wad), str(scene)])
        assert "all 1 sector areas match" in result.stdout
        result = run([viewer, str(scene), "--inspect"])
        assert "12 triangles, 0 sprites" in result.stdout
        good_scene = scene.read_bytes()
        assert good_scene[:8] == b"DRMAP003"
        asset_count, batch_count, sprite_count = struct.unpack_from("<III", good_scene, 16)
        assert (asset_count, batch_count, sprite_count) == (2, 3, 0)
        offset = 44 + 32768
        images = {}
        for _ in range(asset_count):
            label, width, height, left, top = struct.unpack_from("<8sIIii", good_scene, offset)
            offset += 24
            images[label.rstrip(b"\0").upper()] = good_scene[offset:offset + width * height * 4]
            offset += width * height * 4
        wall = images[b"WALL"]
        assert wall[0] == 0 and wall[3] == 255
        assert wall[7] == 0 and wall[12] == 13 and wall[15] == 255
        assert good_scene[44:48] == bytes([0, 0, 0, 255])
        checks += 5
        collision_offset = collision_start(good_scene)
        sector_count, line_count, region_count = struct.unpack_from("<3I", good_scene, collision_offset)
        assert (sector_count, line_count, region_count) == (1, 4, 2)
        assert struct.unpack_from("<ff", good_scene, collision_offset + 12) == (0, 4)
        checks += 1
        bottle_patch = struct.pack("<HHhhII", 2, 4, 1, 2, 16, 25)
        bottle_patch += bytes([0, 4, 0, 1, 2, 3, 4, 0, 255, 0, 4, 0, 5, 6, 7, 8, 0, 255])
        bottle_lumps = [(key, contents + struct.pack("<hhHHH", 96, 64, 0, 2014, 7) if key == "THINGS" else contents)
                        for key, contents in lumps]
        bottle_lumps.append(("BON1A0", bottle_patch))
        wad.write_bytes(make_wad(bottle_lumps))
        bottle_scene = root / "bottle.drmap"
        run([converter, str(wad), str(bottle_scene)])
        data = bottle_scene.read_bytes()
        sprite_offset = collision_start(data) - 20
        assert struct.unpack_from("<f", data, sprite_offset + 12)[0] == 2 / 32
        checks += 1
        texture = dict(lumps)["TEXTURE1"][8:]
        sky_texture = name("SKY1") + texture[8:]
        sky_lumps = []
        for key, contents in lumps:
            if key == "TEXTURE1":
                contents = struct.pack("<III", 2, 12, 12 + len(texture)) + texture + sky_texture
            if key == "SECTORS":
                contents = contents[:12] + name("F_SKY1") + contents[20:]
            sky_lumps.append((key, contents))
        wad.write_bytes(make_wad(sky_lumps))
        sky_scene = root / "sky.drmap"
        run([converter, str(wad), str(sky_scene)])
        data = sky_scene.read_bytes()
        assets, batches, sprites = struct.unpack_from("<3I", data, 16)
        offset = 44 + 32768
        names = []
        for _ in range(assets):
            label, width, height = struct.unpack_from("<8sII", data, offset)
            names.append(label.rstrip(b"\0"))
            offset += 24 + width * height * 4
        skies = []
        for _ in range(batches):
            asset, shade, sky, count, owner, plane = struct.unpack_from("<6I", data, offset)
            if sky:
                skies.append((names[asset], owner, plane))
            offset += 24 + count * 20
        assert skies == [(b"SKY1", 0, 2)]
        checks += 1
        mus = b"MUS\x1a" + struct.pack("<6H", 1, 16, 0, 0, 0, 0) + b"\x60"
        wad.write_bytes(make_wad(lumps + [("D_RUNNIN", mus)]))
        song_scene = root / "song.drmap"
        run([converter, str(wad), str(song_scene)])
        midi = (root / "song.mid").read_bytes()
        assert midi[:4] == b"MThd" and midi[14:18] == b"MTrk" and midi[-4:] == b"\0\xff\x2f\0"
        checks += 1
        wad.write_bytes(make_wad(door_room()))
        door_scene = root / "door.drmap"
        run([converter, str(wad), str(door_scene)])
        run([viewer, str(door_scene), "--inspect"])
        data = door_scene.read_bytes()
        offset = collision_start(data)
        assert struct.unpack_from("<3I", data, offset) == (2, 7, 4)
        assert struct.unpack_from("<ffII", data, offset + 12 + 16) == (0, 0, 0, 7)
        assert struct.unpack_from("<5I", data, offset + 12 + 32 + 6 * 36 + 16) == (0, 1, 4, 1, 7)
        checks += 1
        shapes = [
            ([[(0, 0), (0, 128), (128, 128), (128, 0)], [(32, 32), (96, 32), (96, 96), (32, 96)]], 32),
            ([[(0, 0), (0, 64), (64, 64), (64, 0)], [(128, 0), (128, 64), (192, 64), (192, 0)]], 24),
            ([[(0, 0), (0, 128), (64, 128), (64, 64), (128, 64), (128, 0)]], 20),
        ]
        for contours, triangles in shapes:
            wad.write_bytes(make_wad(outline_room(contours)))
            result = run([converter, str(wad), str(root / "shape.drmap")])
            assert f"{triangles} triangles" in result.stdout, result.stdout
            run([viewer, str(root / "shape.drmap"), "--inspect"])
            checks += 1
        bad_cases = [b"", b"IWAD", b"NOPE" + good_wad[4:]]
        directory_bad = bytearray(good_wad)
        struct.pack_into("<I", directory_bad, 8, len(good_wad) + 1)
        bad_cases.append(directory_bad)
        lump_bad = bytearray(good_wad)
        directory = struct.unpack_from("<I", good_wad, 8)[0]
        struct.pack_into("<I", lump_bad, directory + 16 + 4, 0xFFFFFFFF)
        bad_cases.append(lump_bad)
        for label in ("VERTEXES", "SIDEDEFS", "SECTORS", "TEXTURE1", "PATCH"):
            bad_cases.append(make_wad([(key, contents[:-1] if key == label else contents) for key, contents in lumps]))
        for label, field in (("SEGS", 8), ("SSECTORS", 2), ("SIDEDEFS", 28), ("LINEDEFS", 0)):
            replacements = []
            for key, contents in lumps:
                if key == label:
                    contents = bytearray(contents)
                    struct.pack_into("<H", contents, field, 65535)
                replacements.append((key, contents))
            bad_cases.append(make_wad(replacements))
        for data in bad_cases:
            wad.write_bytes(data)
            run([converter, str(wad), str(root / "bad.drmap")], good=False)
            checks += 1
        for data in (b"", good_scene[:43], good_scene[:-1], b"WRONG001" + good_scene[8:], good_scene + b"extra"):
            scene.write_bytes(data)
            run([viewer, str(scene), "--inspect"], good=False)
            checks += 1
        for field, value in ((16, 0xFFFFFFFF), (28, 0x7FC00000), (44 + 32768 + 8, 0), (44 + 32768 + 16, 0x80000000),
                             (collision_offset, 0), (collision_offset + 12 + 4, 0xBF800000),
                             (collision_offset + 12 + 16 + 16, 99),
                             (collision_offset + 12 + 16 + line_count * 36 + 24, 99)):
            data = bytearray(good_scene)
            struct.pack_into("<I", data, field, value)
            scene.write_bytes(data)
            run([viewer, str(scene), "--inspect"], good=False)
            checks += 1
    print(f"Passed {checks} synthetic WAD, scene, palette, and transparency checks")


if __name__ == "__main__":
    main()
