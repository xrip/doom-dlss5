import pathlib
import struct
import sys
import tempfile

from check_wad import run


def check_midi(data):
    if data[:14] != b"MThd\0\0\0\6\0\0\0\1\0\x8c" or data[14:18] != b"MTrk":
        raise AssertionError("Expected format-0 MIDI at 140 ticks per second")
    length = struct.unpack_from(">I", data, 18)[0]
    if len(data) != 22 + length:
        raise AssertionError("MIDI track size mismatch")
    position = 22
    ticks = 0
    notes = 0
    ended = False
    while position < len(data):
        delay = 0
        for _ in range(4):
            value = data[position]
            position += 1
            delay = (delay << 7) | (value & 127)
            if not value & 128:
                break
        else:
            raise AssertionError("Invalid MIDI delta")
        ticks += delay
        status = data[position]
        position += 1
        if status == 255:
            kind, count = data[position:position + 2]
            position += 2
            if count & 128:
                raise AssertionError("Unexpected long metadata")
            payload = data[position:position + count]
            position += count
            if kind == 81 and payload != b"\x0f\x42\x40":
                raise AssertionError("Wrong MUS tempo")
            if kind == 47:
                ended = True
                if count != 0 or position != len(data):
                    raise AssertionError("Invalid MIDI end marker")
        elif 128 <= status < 240:
            count = 1 if status >> 4 in (12, 13) else 2
            payload = data[position:position + count]
            if len(payload) != count or any(value >= 128 for value in payload):
                raise AssertionError("Invalid MIDI event")
            notes += status >> 4 == 9
            position += count
        else:
            raise AssertionError("Unexpected MIDI status")
    if not ended or not notes or not ticks:
        raise AssertionError("Missing music score")


def main():
    converter, viewer, wad, interaction = sys.argv[1:]
    songs = ["D_RUNNIN", "D_STALKS", "D_COUNTD", "D_BETWEE", "D_DOOM", "D_THE_DA", "D_SHAWN", "D_DDTBLU",
             "D_IN_CIT", "D_DEAD", "D_STLKS2", "D_THEDA2", "D_DOOM2", "D_DDTBL2", "D_RUNNI2", "D_DEAD2",
             "D_STLKS3", "D_ROMERO", "D_SHAWN2", "D_MESSAG", "D_COUNT2", "D_DDTBL3", "D_AMPIE", "D_THEDA3",
             "D_ADRIAN", "D_MESSG2", "D_ROMER2", "D_TENSE", "D_SHAWN3", "D_OPENIN", "D_EVIL", "D_ULTIMA"]
    with tempfile.TemporaryDirectory(prefix="doom2-level-check-") as temporary:
        for number in range(1, 33):
            map_name = f"MAP{number:02}"
            scene = pathlib.Path(temporary) / f"{map_name}.drmap"
            result = run([converter, wad, str(scene), map_name])
            if f"Converted {songs[number - 1]} " not in result.stdout:
                raise AssertionError(f"Wrong music for {map_name}")
            run([viewer, str(scene), "--inspect"])
            run([interaction, str(scene), "--coverage"])
            check_midi(scene.with_suffix(".mid").read_bytes())
    print("All 32 Doom II levels converted, loaded, and matched to valid MIDI music")


if __name__ == "__main__":
    main()
