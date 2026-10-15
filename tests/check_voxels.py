import math
import pathlib
import struct
import sys
import tempfile

from check_wad import make_wad, room, run


def kvx():
    dimensions = (2, 1, 1)
    table_size = 4 * 3 + 2 * 2 * 2
    slabs = bytes([0, 1, 61, 10, 0, 1, 62, 10])
    header = struct.pack("<I3I3i", 24 + table_size + len(slabs), *dimensions, 256, 0, 256)
    tables = struct.pack("<3I4H", table_size, table_size + 4, table_size + 8, 0, 4, 0, 4)
    return header + tables + slabs + bytes(component for color in range(256) for component in (min(color, 63),) * 3)


def read_pack(data):
    assert data[:16] == b"DRVOX001MAP01\0\0\0"
    assets, sprites = struct.unpack_from("<2I", data, 16)
    offset = 24
    meshes = {}
    for _ in range(assets):
        name, count, spin = struct.unpack_from("<8sIf", data, offset)
        offset += 16
        points = [struct.unpack_from("<5f", data, offset + vertex * 20) for vertex in range(count)]
        meshes[name.rstrip(b"\0").decode()] = (count, spin, points)
        offset += count * 20
    actors = [struct.unpack_from("<If", data, offset + actor * 8) for actor in range(sprites)]
    assert offset + sprites * 8 == len(data)
    return meshes, actors


def main():
    converter, viewer = sys.argv[1:]
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        lumps = room()
        things = dict(lumps)["THINGS"] + struct.pack("<hhHHH", 48, 64, 90, 2011, 7)
        lumps = [(key, things if key == "THINGS" else data) for key, data in lumps]
        lumps.append(("STIMA0", dict(lumps)["PATCH"]))
        wad, scene, model, definition = [root / name for name in ("test.wad", "test.drmap", "test.kvx", "VOXELDEF")]
        wad.write_bytes(make_wad(lumps))
        original = kvx()
        model.write_bytes(original)
        definition.write_text('/* two keys, one unused */ STIM XXXX = "test" { Scale = 2 AngleOffset = 270 PlacedSpin = 10 UseActorPitch }\n')
        base = [converter, str(wad), str(scene)]
        run(base)
        original_scene = scene.read_bytes()
        command = base + ["--voxels", str(root), "--voxeldef", str(definition)]
        run(command)
        assert scene.read_bytes() == original_scene, "voxel mode must not change DRMAP006"
        pack = scene.with_suffix(".drvox")
        valid_pack = pack.read_bytes()
        meshes, actors = read_pack(valid_pack)
        count, spin, points = meshes["STIMA0"]
        assert count == 36, "adjacent equal-color cells must merge to six exterior quads"
        assert spin == 10 and len(actors) == 1 and math.isclose(actors[0][1], math.pi / 2, abs_tol=1e-6)
        for axis, expected in enumerate(((-1 / 16, 1 / 16), (0, 1 / 16), (0, 1 / 16))):
            actual = min(point[axis] for point in points), max(point[axis] for point in points)
            assert all(math.isclose(value, bound, abs_tol=1e-6) for value, bound in zip(actual, expected)), (axis, actual)
        assert all(math.isclose(point[3], 40.5 / 256) and point[4] == 0.5 for point in points)
        center = (0, 1 / 32, 1 / 32)
        for first in range(0, count, 3):
            triangle = points[first:first + 3]
            edge_a = [triangle[1][axis] - triangle[0][axis] for axis in range(3)]
            edge_b = [triangle[2][axis] - triangle[0][axis] for axis in range(3)]
            normal = [edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
                      edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
                      edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0]]
            assert sum(normal[axis] * (triangle[0][axis] - center[axis]) for axis in range(3)) > 0
        inspect = [viewer, str(scene), "--inspect", "--voxels"]
        assert "1/1 objects replaced" in run(inspect).stdout
        for damaged in (valid_pack[:-1], valid_pack + b"junk", valid_pack[:8] + b"MAP02\0\0\0" + valid_pack[16:],
                        valid_pack[:32] + struct.pack("<I", 0xFFFFFFFF) + valid_pack[36:]):
            pack.write_bytes(damaged)
            run(inspect, good=False)
        pack.write_bytes(valid_pack)
        second = root / "second.def"
        second.write_text('STIMA = "test.kvx" { Scale = 1 AngleOffset = 270 PlacedSpin = -20 }')
        run(command + ["--voxeldef", str(second)])
        count, spin, points = read_pack(pack.read_bytes())[0]["STIMA0"]
        assert count == 36 and spin == -20
        assert math.isclose(max(point[1] for point in points), 1 / 32, abs_tol=1e-6)
        unit = 1 / 32
        default_pack = None
        for angle, expected_x, expected_z in (
            (None, (0, unit), (-unit, unit)),
            (0, (0, unit), (-unit, unit)),
            (90, (-unit, unit), (-unit, 0)),
            (180, (-unit, 0), (-unit, unit)),
            (270, (-unit, unit), (0, unit)),
        ):
            option = "" if angle is None else f"AngleOffset = {angle}"
            second.write_text(f'STIMA = "test.kvx" {{ {option} }}')
            run(command + ["--voxeldef", str(second)])
            facing_pack = pack.read_bytes()
            if angle is None:
                default_pack = facing_pack
            elif angle == 0:
                assert facing_pack == default_pack, "default facing equals explicit AngleOffset 0"
            points = read_pack(facing_pack)[0]["STIMA0"][2]
            for axis, expected in ((0, expected_x), (2, expected_z)):
                actual = min(point[axis] for point in points), max(point[axis] for point in points)
                assert all(math.isclose(value, bound, abs_tol=1e-6) for value, bound in zip(actual, expected)), (
                    "voxel facing", angle, axis, actual, expected)
        for damaged in (original[:27], original[:28] + b"\0\0\0\0" + original[32:],
                        original[:48] + bytes([0, 0, 61, 10]) + original[52:]):
            model.write_bytes(damaged)
            run(command, good=False)
        model.unlink()
        assert "keeping sprite STIMA0" in run(command).stderr
        assert "objects replaced" not in run(inspect).stdout
        definition.write_text('STIM = "../test" {}')
        run(command, good=False)
        definition.write_text('STIM = "test" { Scale = nan }')
        run(command, good=False)
    print("Passed KVX surfaces, greedy meshes, pivots, palette, yaw, definitions, fallback, and native pack checks")


if __name__ == "__main__":
    main()
