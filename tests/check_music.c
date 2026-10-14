#include "mus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(bool condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "Music test failed: %s\n", name);
        exit(EXIT_FAILURE);
    }
}

static size_t make_mus(uint8_t *data, const uint8_t *score, size_t score_size) {
    memset(data, 0, 16);
    memcpy(data, "MUS\x1a", 4);
    data[4] = (uint8_t)score_size;
    data[5] = (uint8_t)(score_size >> 8);
    data[6] = 16;
    memcpy(data + 16, score, score_size);
    return 16 + score_size;
}

static void conversion_test(void) {
    const uint8_t score[] = {
        0xc0, 5, 50, 2,
        0x9f, 0xbc, 100, 3,
        0x1f, 61,
        0xaf, 255, 1,
        0xcf, 4, 100, 2,
        0x99, 64, 4,
        0xd0, 2,
        0x30, 12,
        0x60
    };
    const uint8_t expected_track[] = {
        0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40,
        0, 0xb0, 11, 50,
        2, 0x99, 60, 100,
        3, 0x99, 61, 100,
        0, 0xe9, 64, 127,
        1, 0xb9, 10, 100,
        2, 0x9a, 64, 64,
        6, 0xb0, 126, 3,
        0, 0xff, 0x2f, 0
    };
    uint8_t mus[64];
    size_t mus_size = make_mus(mus, score, sizeof(score));
    mus[8] = 2;
    uint8_t *midi = NULL;
    size_t midi_size = 0;
    expect(mus_to_midi(mus, mus_size, &midi, &midi_size), "convert valid score");
    expect(midi_size == 22 + sizeof(expected_track), "MIDI file size");
    expect(memcmp(midi, "MThd\0\0\0\6\0\0\0\1\0\x8cMTrk", 18) == 0, "MIDI header");
    expect(midi[18] == 0 && midi[19] == 0 && midi[20] == 0 &&
           midi[21] == sizeof(expected_track), "track length");
    expect(memcmp(midi + 22, expected_track, sizeof(expected_track)) == 0,
           "channel, controller, velocity, pitch, and time events");
    free(midi);
}

static void invalid_test(void) {
    uint8_t data[32];
    const uint8_t truncated_unused_event[] = {0x70};
    size_t size = make_mus(data, truncated_unused_event, sizeof(truncated_unused_event));
    uint8_t *midi = (uint8_t *)1;
    size_t midi_size = 1;
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject truncated unused event");
    expect(midi == NULL && midi_size == 0, "clear output on failure");

    const uint8_t end_measure_only[] = {0x50};
    size = make_mus(data, end_measure_only, sizeof(end_measure_only));
    expect(!mus_to_midi(data, size, &midi, &midi_size), "require score-finish event 6");

    data[0] = 'X';
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject bad signature");
    data[0] = 'M';
    data[4] = 0xff;
    data[5] = 0xff;
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject score past input");

    const uint8_t truncated_event[] = {0x10};
    size = make_mus(data, truncated_event, sizeof(truncated_event));
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject truncated note");

    const uint8_t truncated_delay[] = {0x81, 60, 0x80};
    size = make_mus(data, truncated_delay, sizeof(truncated_delay));
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject truncated delay");

    const uint8_t finish[] = {0x60};
    size = make_mus(data, finish, sizeof(finish));
    data[8] = 13;
    expect(mus_to_midi(data, size, &midi, &midi_size), "accept thirteen primary channels");
    free(midi);
    data[8] = 15;
    expect(mus_to_midi(data, size, &midi, &midi_size), "accept fifteen primary channels");
    free(midi);
    data[8] = 16;
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject more than fifteen primary channels");
    data[8] = 0;
    data[10] = 5;
    expect(mus_to_midi(data, size, &midi, &midi_size), "accept five secondary channels");
    free(midi);
    data[10] = 6;
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject too many secondary channels");

    data[10] = 0;
    data[12] = 1;
    expect(!mus_to_midi(data, size, &midi, &midi_size), "reject short instrument table");
    data[6] = 18;
    data[18] = 0x60;
    size = 19;
    expect(mus_to_midi(data, size, &midi, &midi_size), "accept complete instrument table");
    free(midi);

    expect(!mus_to_midi(data, size, NULL, &midi_size), "reject null output argument");
}

int main(void) {
    conversion_test();
    invalid_test();
    puts("Music checks passed");
    return EXIT_SUCCESS;
}
