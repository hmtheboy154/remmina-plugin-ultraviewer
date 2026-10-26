/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Remote Computer Audio Playback
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_AUDIO_H
#define UV_AUDIO_H

#include "uv_types.h"

typedef struct UvAudio UvAudio;

UvAudio *uv_audio_new(void);
void uv_audio_decode_and_play(UvAudio *audio, const uint8_t *g722_data, size_t len);
void uv_audio_play_silence(UvAudio *audio, size_t pcm_bytes);
void uv_audio_free(UvAudio *audio);

size_t uv_g722_decode(const uint8_t *input, size_t in_len, int16_t *output, size_t out_capacity);

#endif /* UV_AUDIO_H */
