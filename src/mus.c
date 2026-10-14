#include "mus.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} MidiBuffer;

typedef struct {
    const uint8_t *data;
    size_t position;
    size_t end;
} MusReader;

static bool append(MidiBuffer *buffer, const uint8_t *data, size_t size) {
    if (size > SIZE_MAX - buffer->size) return false;
    size_t needed = buffer->size + size;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 128;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        uint8_t *grown = realloc(buffer->data, capacity);
        if (!grown) return false;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size = needed;
    return true;
}

static bool byte(MidiBuffer *buffer, uint8_t value) {
    return append(buffer, &value, 1);
}

static bool midi_vlq(MidiBuffer *buffer, uint32_t value) {
    if (value > 0x0fffffff) return false;
    uint8_t bytes[4];
    size_t count = 1;
    bytes[3] = (uint8_t)(value & 0x7f);
    while ((value >>= 7) != 0) bytes[3 - count++] = (uint8_t)((value & 0x7f) | 0x80);
    return append(buffer, bytes + 4 - count, count);
}

static bool read_byte(MusReader *reader, uint8_t *value) {
    if (reader->position >= reader->end) return false;
    *value = reader->data[reader->position++];
    return true;
}

static bool read_delay(MusReader *reader, uint32_t *delay) {
    uint32_t value = 0;
    for (unsigned count = 0; count < 5; ++count) {
        uint8_t next;
        if (!read_byte(reader, &next) || value > (UINT32_MAX >> 7)) return false;
        value = (value << 7) | (next & 0x7f);
        if (!(next & 0x80)) {
            if (value > 0x0fffffff) return false;
            *delay = value;
            return true;
        }
    }
    return false;
}

static uint8_t midi_channel(uint8_t channel) {
    if (channel == 15) return 9;
    return (uint8_t)(channel >= 9 ? channel + 1 : channel);
}

static bool emit_event(MidiBuffer *track, uint32_t delay, uint8_t status,
                       uint8_t first, uint8_t second, bool one_data_byte) {
    return midi_vlq(track, delay) && byte(track, status) && byte(track, first) &&
           (one_data_byte || byte(track, second));
}

bool mus_to_midi(const uint8_t *data, size_t size, uint8_t **midi, size_t *midi_size) {
    static const uint8_t controller_map[] = {0, 0, 1, 7, 10, 11, 91, 93, 64, 67};
    static const uint8_t system_map[] = {120, 123, 126, 127, 121};
    if (!midi || !midi_size) return false;
    *midi = NULL;
    *midi_size = 0;
    if (!data || size < 16 || memcmp(data, "MUS\x1a", 4)) return false;

    size_t score_size = (size_t)data[4] | (size_t)data[5] << 8;
    size_t score_start = (size_t)data[6] | (size_t)data[7] << 8;
    size_t primary_channels = (size_t)data[8] | (size_t)data[9] << 8;
    size_t secondary_channels = (size_t)data[10] | (size_t)data[11] << 8;
    size_t instrument_count = (size_t)data[12] | (size_t)data[13] << 8;
    size_t instrument_end = 16 + instrument_count * 2;
    if (primary_channels > 15 || secondary_channels > 5 || score_start < instrument_end ||
        score_start > size || score_size > size - score_start) return false;

    MusReader reader = {data, score_start, score_start + score_size};
    MidiBuffer track = {0};
    const uint8_t tempo[] = {0, 0xff, 0x51, 3, 0x0f, 0x42, 0x40};
    uint8_t velocities[16];
    memset(velocities, 64, sizeof(velocities));
    if (!append(&track, tempo, sizeof(tempo))) goto failed;

    uint32_t pending_delay = 0;
    bool ended = false;
    while (reader.position < reader.end) {
        uint8_t descriptor;
        if (!read_byte(&reader, &descriptor)) goto failed;
        uint8_t channel = descriptor & 0x0f;
        uint8_t type = (descriptor >> 4) & 7;
        uint8_t midi_ch = midi_channel(channel);
        uint8_t first, second;
        bool ok = true;

        switch (type) {
            case 0:
                if (!read_byte(&reader, &first) || first > 127) goto failed;
                ok = emit_event(&track, pending_delay, (uint8_t)(0x80 | midi_ch), first, 0, false);
                break;
            case 1:
                if (!read_byte(&reader, &first)) goto failed;
                second = velocities[channel];
                if (first & 0x80) {
                    first &= 0x7f;
                    if (!read_byte(&reader, &second) || second > 127) goto failed;
                    velocities[channel] = second;
                }
                ok = emit_event(&track, pending_delay, (uint8_t)(0x90 | midi_ch), first, second, false);
                break;
            case 2:
                if (!read_byte(&reader, &first)) goto failed;
                ok = midi_vlq(&track, pending_delay) && byte(&track, (uint8_t)(0xe0 | midi_ch)) &&
                     byte(&track, (uint8_t)((first << 6) & 0x7f)) &&
                     byte(&track, (uint8_t)(first >> 1));
                break;
            case 3:
                if (!read_byte(&reader, &first) || first < 10 || first > 14) goto failed;
                ok = emit_event(&track, pending_delay, (uint8_t)(0xb0 | midi_ch),
                                system_map[first - 10],
                                first == 12 ? (uint8_t)(primary_channels + 1) : 0, false);
                break;
            case 4:
                if (!read_byte(&reader, &first) || first >= sizeof(controller_map) ||
                    !read_byte(&reader, &second) || second > 127) goto failed;
                if (first == 0) {
                    ok = emit_event(&track, pending_delay, (uint8_t)(0xc0 | midi_ch), second, 0, true);
                } else {
                    ok = emit_event(&track, pending_delay, (uint8_t)(0xb0 | midi_ch),
                                    controller_map[first], second, false);
                }
                break;
            case 5:
                if (descriptor & 0x80) {
                    uint32_t delay;
                    if (!read_delay(&reader, &delay) || delay > 0x0fffffff - pending_delay) goto failed;
                    pending_delay += delay;
                }
                continue;
            case 6:
                if (reader.position != reader.end || descriptor & 0x80) goto failed;
                if (!midi_vlq(&track, pending_delay) || !byte(&track, 0xff) ||
                    !byte(&track, 0x2f) || !byte(&track, 0)) goto failed;
                ended = true;
                break;
            case 7:
                if (!read_byte(&reader, &first)) goto failed;
                if (descriptor & 0x80) {
                    uint32_t delay;
                    if (!read_delay(&reader, &delay) || delay > 0x0fffffff - pending_delay) goto failed;
                    pending_delay += delay;
                }
                continue;
        }
        if (!ok) goto failed;
        pending_delay = 0;
        if ((descriptor & 0x80) && !read_delay(&reader, &pending_delay)) goto failed;
    }
    if (!ended || track.size > UINT32_MAX || track.size > SIZE_MAX - 22) goto failed;

    size_t output_size = 22 + track.size;
    uint8_t *output = malloc(output_size);
    if (!output) goto failed;
    memcpy(output, "MThd\0\0\0\6\0\0\0\1\0\x8cMTrk", 18);
    output[18] = (uint8_t)(track.size >> 24);
    output[19] = (uint8_t)(track.size >> 16);
    output[20] = (uint8_t)(track.size >> 8);
    output[21] = (uint8_t)track.size;
    memcpy(output + 22, track.data, track.size);
    free(track.data);
    *midi = output;
    *midi_size = output_size;
    return true;

failed:
    free(track.data);
    return false;
}
