#include "mus.h"
#include "scene.h"

#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) fail("Usage: mus-convert input.mus output.mid");
    size_t input_size;
    uint8_t *input = read_file(argv[1], &input_size);
    uint8_t *midi;
    size_t midi_size;
    if (!mus_to_midi(input, input_size, &midi, &midi_size)) fail("Invalid MUS file: %s", argv[1]);
    free(input);
    write_file(argv[2], midi, midi_size);
    free(midi);
    return EXIT_SUCCESS;
}
