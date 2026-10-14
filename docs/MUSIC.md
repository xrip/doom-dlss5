# MUS conversion and WinMM playback

`mus-convert INPUT.mus OUTPUT.mid` converts a standalone MUS score.
`wad-convert` selects the original MAP01..MAP32 song and writes a `.mid`
beside the scene. The viewer loops that file by default; `--no-music`
disables playback and `--music FILE.mid` selects another track.

`mus_to_midi` returns a malloc-owned MIDI buffer. Failure returns false and
clears the output pointer and size. Header, instrument, event, and delay
bounds are checked. Channel 15 maps to MIDI percussion channel 9; other
channels skip that MIDI channel. Controllers, program changes, pitch bend,
and remembered velocity are converted. A final score-end event is required.
Format-0 MIDI uses division 140 and tempo 1,000,000 microseconds per quarter
note, preserving one MUS tick as 1/140 second.

`music_start`, `music_update`, and `music_stop` use the Windows WinMM MCI
sequencer. The update checks for completion and restarts the score. Stop
closes the device. Failure leaves the viewer usable without music.
There is no separate synth, audio thread, or raylib audio backend.
Non-Windows builds retain conversion and use silent playback stubs.

See [VALIDATION.md](VALIDATION.md) for the original-track and WinMM checks.
