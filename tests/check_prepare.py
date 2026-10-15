import pathlib
import os
import shutil
import struct
import sys
import tempfile

from check_voxels import kvx
from check_wad import make_wad, room, run


def cache_directory(result):
    return pathlib.Path(next(line[7:] for line in result.stdout.splitlines() if line.startswith("Cache: ")))


def main():
    converter, viewer = map(pathlib.Path, sys.argv[1:])
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        game = root / "game with spaces"
        game.mkdir()
        local_viewer = game / viewer.name
        shutil.copy2(viewer, local_viewer)
        local_converter = game / converter.name
        shutil.copy2(converter, local_converter)
        lumps = room()
        things = dict(lumps)["THINGS"] + struct.pack("<hhHHH", 48, 64, 90, 2011, 7)
        first = [(name, things if name == "THINGS" else data) for name, data in lumps]
        second = [("MAP02" if name == "MAP01" else name, data) for name, data in first]
        wad = root / "original game.WAD"
        music = struct.pack("<4s6H", b"MUS\x1a", 1, 16, 1, 0, 0, 0) + b"\x60"
        wad.write_bytes(make_wad(first + second + [("STIMA0", dict(lumps)["PATCH"]),
            ("D_RUNNIN", music), ("D_STALKS", music)]))
        command = [str(local_viewer), str(wad)]
        result = run(command + ["--inspect"])
        assert "MAP01:" in result.stdout
        cache = cache_directory(result)
        assert cache.parent == game / "cache"
        assert (cache / "MAP01.drmap").is_file() and (cache / "MAP01.mid").is_file()
        assert not (cache / "MAP01.drvox").exists()
        before = (cache / "MAP01.drmap").stat().st_mtime_ns
        cached = run(command + ["--inspect"])
        assert "Using cached MAP01" in cached.stdout
        assert (cache / "MAP01.drmap").stat().st_mtime_ns == before
        (cache / "MAP01.mid").unlink()
        assert "Preparing MAP01" in run(command + ["--inspect"]).stdout
        assert (cache / "MAP01.mid").is_file()
        (game / "voxels").mkdir()
        (game / "voxels/test.kvx").write_bytes(kvx())
        (game / "VOXELDEF1.txt").write_text('STIM = "test" {}')
        (game / "voxeldef.txt").write_text("// no overrides\n")
        result = run(command + ["map02", "--inspect"])
        assert "MAP02:" in result.stdout and "1/1 objects replaced" in result.stdout
        voxel_cache = cache_directory(result)
        assert voxel_cache != cache and (voxel_cache / "MAP02.drvox").is_file()
        assert "Using cached MAP02" in run(command + ["MAP02", "--inspect"]).stdout
        (voxel_cache / "MAP02.drvox").unlink()
        assert "Preparing MAP02" in run(command + ["MAP02", "--inspect"]).stdout
        (voxel_cache / "MAP02.ready").write_bytes(b"bad")
        assert "Preparing MAP02" in run(command + ["MAP02", "--inspect"]).stdout
        model = game / "voxels/test.kvx"
        stamp = model.stat()
        os.utime(model, ns=(stamp.st_atime_ns, stamp.st_mtime_ns + 1_000_000_000))
        changed = run(command + ["MAP02", "--inspect"])
        assert cache_directory(changed) != voxel_cache and "Preparing MAP02" in changed.stdout
        (game / "voxeldef.txt").write_text("// changed definition\n")
        changed_definition = run(command + ["MAP02", "--inspect"])
        assert cache_directory(changed_definition) != cache_directory(changed)
        wad.write_bytes(wad.read_bytes() + b"changed source")
        changed_wad = run(command + ["MAP02", "--inspect"])
        assert cache_directory(changed_wad) != cache_directory(changed_definition)
        assert "objects replaced" not in run(command + ["MAP02", "--no-voxels", "--inspect"]).stdout
        run(command + ["MAP00", "--inspect"], good=False)
        run(command + ["MAP33", "--inspect"], good=False)
        run(command + ["MAP03", "--inspect"], good=False)
        run([str(local_viewer), str(cache / "MAP01.drmap"), "MAP02", "--inspect"], good=False)
        (game / "VOXELDEF1.txt").unlink()
        run(command + ["MAP02", "--voxels", "--inspect"], good=False)
        local_converter.unlink()
        run(command + ["--inspect"], good=False)
    print("Passed WAD cache hits, input changes, missing outputs, music, map selection, voxel discovery and errors without GPU/NGX")


if __name__ == "__main__":
    main()
