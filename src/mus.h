#pragma once

#include <stddef.h>
#include <stdint.h>

bool mus_to_midi(const uint8_t *data, size_t size, uint8_t **midi, size_t *midi_size);
