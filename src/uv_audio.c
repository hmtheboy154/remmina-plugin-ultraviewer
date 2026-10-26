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

#include "uv_audio.h"
#include <pulse/simple.h>
#include <pulse/error.h>

typedef struct {
    int32_t s;
    int32_t sp;
    int32_t sz;
    int32_t r[3];
    int32_t a[3];
    int32_t ap[3];
    int32_t p[3];
    int32_t d[7];
    int32_t b[7];
    int32_t bp[7];
    int32_t sg[7];
    int32_t nb;
    int32_t det;
} Band;

typedef struct {
    int32_t x[24];
    Band band[2];
} G722Decoder;

static inline int32_t sat(int32_t v)
{
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return v;
}

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static const int32_t WL[8] = { -60, -30, 58, 172, 334, 538, 1198, 3042 };
static const size_t RL42[16] = { 0, 7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3, 2, 1, 0 };
static const int32_t ILB[32] = {
    2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383, 2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
    2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371, 3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008
};
static const int32_t WH[3] = { 0, -214, 798 };
static const size_t RH2[4] = { 2, 1, 2, 1 };
static const int32_t QM2[4] = { -7408, -1616, 7408, 1616 };
static const int32_t QM4[16] = {
    0, -20456, -12896, -8968, -6288, -4240, -2584, -1200,
    20456, 12896, 8968, 6288, 4240, 2584, 1200, 0
};
static const int32_t QM6[64] = {
    -136, -136, -136, -136, -24808, -21904, -19008, -16704, -14984, -13512, -12280, -11192, -10232, -9360, -8576,
    -7856, -7192, -6576, -6000, -5456, -4944, -4464, -4008, -3576, -3168, -2776, -2400, -2032, -1688, -1360, -1040,
    -728, 24808, 21904, 19008, 16704, 14984, 13512, 12280, 11192, 10232, 9360, 8576, 7856, 7192, 6576, 6000, 5456,
    4944, 4464, 4008, 3576, 3168, 2776, 2400, 2032, 1688, 1360, 1040, 728, 432, 136, -432, -136
};
static const int32_t QMF[12] = { 3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11 };

static void g722_init(G722Decoder *dec)
{
    memset(dec, 0, sizeof(*dec));
    dec->band[0].det = 32;
    dec->band[1].det = 8;
}

static void g722_block4(G722Decoder *dec, int which, int32_t d)
{
    Band *b = &dec->band[which];
    b->d[0] = d;
    b->r[0] = sat(b->s + d);
    b->p[0] = sat(b->sz + d);
    for (int i = 0; i < 3; i++) {
        b->sg[i] = b->p[i] >> 15;
    }
    int32_t wd1 = sat(b->a[1] * 4);
    int32_t wd2 = (b->sg[0] == b->sg[1] ? -wd1 : wd1);
    if (wd2 > 32767) wd2 = 32767;
    int32_t wd3 = (b->sg[0] == b->sg[2] ? 128 : -128);
    wd3 += wd2 >> 7;
    wd3 += (b->a[2] * 32512) >> 15;
    b->ap[2] = clamp_i32(wd3, -12288, 12288);

    b->sg[0] = b->p[0] >> 15;
    b->sg[1] = b->p[1] >> 15;
    wd1 = (b->sg[0] == b->sg[1] ? 192 : -192);
    wd2 = (b->a[1] * 32640) >> 15;
    b->ap[1] = sat(wd1 + wd2);
    wd3 = sat(15360 - b->ap[2]);
    if (b->ap[1] > wd3) {
        b->ap[1] = wd3;
    } else if (b->ap[1] < -wd3) {
        b->ap[1] = -wd3;
    }

    wd1 = (d == 0 ? 0 : 128);
    b->sg[0] = d >> 15;
    for (int i = 1; i < 7; i++) {
        b->sg[i] = b->d[i] >> 15;
        wd2 = (b->sg[i] == b->sg[0] ? wd1 : -wd1);
        wd3 = (b->b[i] * 32640) >> 15;
        b->bp[i] = sat(wd2 + wd3);
    }

    for (int i = 6; i >= 1; i--) {
        b->d[i] = b->d[i - 1];
        b->b[i] = b->bp[i];
    }
    for (int i = 2; i >= 1; i--) {
        b->r[i] = b->r[i - 1];
        b->p[i] = b->p[i - 1];
        b->a[i] = b->ap[i];
    }

    wd1 = (b->a[1] * sat(b->r[1] + b->r[1])) >> 15;
    wd2 = (b->a[2] * sat(b->r[2] + b->r[2])) >> 15;
    b->sp = sat(wd1 + wd2);
    b->sz = 0;
    for (int i = 6; i >= 1; i--) {
        b->sz += (b->b[i] * sat(b->d[i] + b->d[i])) >> 15;
    }
    b->sz = sat(b->sz);
    b->s = sat(b->sp + b->sz);
}

static void g722_decode_byte(G722Decoder *dec, uint8_t code_b, int16_t out_samples[2])
{
    size_t code = (size_t)code_b;
    size_t wd1 = code & 0x3f;
    size_t ihigh = (code >> 6) & 3;
    int32_t wd2 = QM6[wd1];
    wd1 >>= 2;
    wd2 = (dec->band[0].det * wd2) >> 15;
    int32_t rlow = clamp_i32(dec->band[0].s + wd2, -16384, 16383);
    int32_t dlowt = (dec->band[0].det * QM4[wd1]) >> 15;
    int32_t nb = clamp_i32(((dec->band[0].nb * 127) >> 7) + WL[RL42[wd1]], 0, 18432);
    dec->band[0].nb = nb;
    size_t i = (size_t)((nb >> 6) & 31);
    int sh = 8 - (nb >> 11);
    dec->band[0].det = (sh < 0 ? (ILB[i] << -sh) : (ILB[i] >> sh)) << 2;
    g722_block4(dec, 0, dlowt);

    int32_t dhigh = (dec->band[1].det * QM2[ihigh]) >> 15;
    int32_t rhigh = clamp_i32(dhigh + dec->band[1].s, -16384, 16383);
    nb = clamp_i32(((dec->band[1].nb * 127) >> 7) + WH[RH2[ihigh]], 0, 22528);
    dec->band[1].nb = nb;
    i = (size_t)((nb >> 6) & 31);
    sh = 10 - (nb >> 11);
    dec->band[1].det = (sh < 0 ? (ILB[i] << -sh) : (ILB[i] >> sh)) << 2;
    g722_block4(dec, 1, dhigh);

    memmove(&dec->x[0], &dec->x[2], 22 * sizeof(int32_t));
    dec->x[22] = rlow + rhigh;
    dec->x[23] = rlow - rhigh;
    int64_t xout1 = 0;
    int64_t xout2 = 0;
    for (int tap = 0; tap < 12; tap++) {
        xout2 += (int64_t)dec->x[2 * tap] * (int64_t)QMF[tap];
        xout1 += (int64_t)dec->x[2 * tap + 1] * (int64_t)QMF[11 - tap];
    }
    out_samples[0] = (int16_t)(xout1 >> 11);
    out_samples[1] = (int16_t)(xout2 >> 11);
}

size_t uv_g722_decode(const uint8_t *input, size_t in_len, int16_t *output, size_t out_capacity)
{
    if (!input || !output || in_len * 2 > out_capacity) return 0;
    G722Decoder dec;
    g722_init(&dec);
    for (size_t idx = 0; idx < in_len; idx++) {
        g722_decode_byte(&dec, input[idx], &output[idx * 2]);
    }
    return in_len * 2;
}

typedef struct {
    uint8_t *data;
    size_t len;
} AudioPacket;

struct UvAudio {
    G722Decoder decoder;
    GAsyncQueue *queue;
    GThread *worker_thread;
    volatile bool running;
    pa_simple *pa;
};

static gpointer audio_playback_worker(gpointer user_data)
{
    UvAudio *audio = (UvAudio *)user_data;

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 48000;
    ss.channels = 2;

    int error = 0;
    pa_simple *pa = pa_simple_new(NULL, "Remmina", PA_STREAM_PLAYBACK, NULL, "UltraViewer Audio", &ss, NULL, NULL, &error);
    if (!pa) {
        g_warning("[uv_audio] PulseAudio playback init failed: %s", pa_strerror(error));
        return NULL;
    }
    audio->pa = pa;

    while (audio->running) {
        AudioPacket *pkt = (AudioPacket *)g_async_queue_timeout_pop(audio->queue, 200000);
        if (!pkt) continue;
        if (!audio->running) {
            g_free(pkt->data);
            g_free(pkt);
            break;
        }

        if (pkt->data && pkt->len > 0) {
            pa_simple_write(pa, pkt->data, pkt->len, &error);
        }
        g_free(pkt->data);
        g_free(pkt);
    }

    pa_simple_drain(pa, NULL);
    pa_simple_free(pa);
    audio->pa = NULL;
    return NULL;
}

UvAudio *uv_audio_new(void)
{
    UvAudio *audio = g_new0(UvAudio, 1);
    g722_init(&audio->decoder);
    audio->queue = g_async_queue_new();
    audio->running = true;
    audio->worker_thread = g_thread_new("uv_audio", audio_playback_worker, audio);
    return audio;
}

static void queue_audio_data(UvAudio *audio, const uint8_t *data, size_t len)
{
    if (!audio || !audio->running || !data || len == 0) return;

    /* Drop old audio packets if queue grows over ~250ms (48KB) */
    while (g_async_queue_length(audio->queue) > 8) {
        AudioPacket *old = (AudioPacket *)g_async_queue_try_pop(audio->queue);
        if (old) {
            g_free(old->data);
            g_free(old);
        } else {
            break;
        }
    }

    AudioPacket *pkt = g_new0(AudioPacket, 1);
    pkt->data = g_memdup2(data, len);
    pkt->len = len;
    g_async_queue_push(audio->queue, pkt);
}

void uv_audio_decode_and_play(UvAudio *audio, const uint8_t *g722_data, size_t len)
{
    if (!audio || !audio->running || !g722_data || len == 0) return;

    if (len > 64 * 1024) return;

    size_t samples = len * 2;
    size_t pcm_bytes = samples * sizeof(int16_t);
    int16_t *pcm = g_malloc(pcm_bytes);

    for (size_t idx = 0; idx < len; idx++) {
        g722_decode_byte(&audio->decoder, g722_data[idx], &pcm[idx * 2]);
    }

    queue_audio_data(audio, (const uint8_t *)pcm, pcm_bytes);
    g_free(pcm);
}

void uv_audio_play_silence(UvAudio *audio, size_t pcm_bytes)
{
    if (!audio || !audio->running || pcm_bytes == 0) return;
    if (pcm_bytes > 256 * 1024) pcm_bytes = 256 * 1024;

    uint8_t *zeros = g_malloc0(pcm_bytes);
    queue_audio_data(audio, zeros, pcm_bytes);
    g_free(zeros);
}

void uv_audio_free(UvAudio *audio)
{
    if (!audio) return;
    audio->running = false;

    /* Wake worker */
    AudioPacket *sentinel = g_new0(AudioPacket, 1);
    g_async_queue_push(audio->queue, sentinel);

    if (audio->worker_thread) {
        g_thread_join(audio->worker_thread);
        audio->worker_thread = NULL;
    }

    while (g_async_queue_length(audio->queue) > 0) {
        AudioPacket *p = (AudioPacket *)g_async_queue_try_pop(audio->queue);
        if (p) {
            g_free(p->data);
            g_free(p);
        }
    }
    g_async_queue_unref(audio->queue);
    g_free(audio);
}
