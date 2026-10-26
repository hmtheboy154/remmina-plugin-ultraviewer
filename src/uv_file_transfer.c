/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - File Transfer Implementation
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_file_transfer.h"
#include "uv_crypto.h"
#include "uv_session.h"
#include <sys/stat.h>

#define UV_FT_CHUNK_SIZE 32000

struct UvFileTransferManager {
    UvSession *session;
    GList *tasks;
    GMutex mutex;
    UvFileTransferNoticeCb notice_cb;
    gpointer callback_data;
};

static void send_utf16_cmd(UvSession *session, const char *cmd)
{
    uint8_t *utf16 = NULL;
    size_t len = 0;
    if (uv_utf8_to_utf16le(cmd, &utf16, &len)) {
        uv_session_queue_outbound(session, true, utf16, len);
        g_free(utf16);
    }
}

static void send_next_chunk(UvFileTransferManager *mgr, UvFileTransferTask *task)
{
    if (!task || !task->fp || task->completed || task->cancelled) return;

    uint8_t buf[UV_FT_CHUNK_SIZE];
    fseeko(task->fp, (off_t)task->offset, SEEK_SET);
    size_t n_read = fread(buf, 1, sizeof(buf), task->fp);

    if (n_read == 0) {
        task->completed = true;
        fclose(task->fp);
        task->fp = NULL;
        if (mgr->notice_cb) {
            char *msg = g_strdup_printf("File sent successfully: %s", task->file_name);
            mgr->notice_cb(msg, mgr->callback_data);
            g_free(msg);
        }
        return;
    }

    /* 1-based offset on wire */
    uint64_t wire_offset = task->offset + 1;
    char header[64];
    snprintf(header, sizeof(header), "15|||%s|||%" PRIu64 "|||", task->id, wire_offset);
    size_t hdr_len = strlen(header);

    uint8_t *payload = g_malloc(hdr_len + n_read);
    memcpy(payload, header, hdr_len);
    memcpy(payload + hdr_len, buf, n_read);

    uv_session_queue_outbound(mgr->session, false, payload, hdr_len + n_read);
    g_free(payload);

    task->offset += n_read;
}

UvFileTransferManager *uv_ft_manager_new(UvSession *session, UvFileTransferNoticeCb notice_cb, gpointer callback_data)
{
    UvFileTransferManager *mgr = g_new0(UvFileTransferManager, 1);
    mgr->session = session;
    mgr->notice_cb = notice_cb;
    mgr->callback_data = callback_data;
    g_mutex_init(&mgr->mutex);
    return mgr;
}

bool uv_ft_send_file(UvFileTransferManager *mgr, const char *local_path)
{
    if (!mgr || !local_path) return false;

    struct stat st;
    if (stat(local_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    FILE *fp = fopen(local_path, "rb");
    if (!fp) return false;

    char *base = g_path_get_basename(local_path);
    UvFileTransferTask *task = g_new0(UvFileTransferTask, 1);
    snprintf(task->id, sizeof(task->id), "%u", g_random_int_range(10000000, 99999999));
    g_strlcpy(task->file_name, base, sizeof(task->file_name));
    g_strlcpy(task->file_path, local_path, sizeof(task->file_path));
    task->file_size = (uint64_t)st.st_size;
    task->offset = 0;
    task->fp = fp;
    task->is_outbound = true;
    g_free(base);

    g_mutex_lock(&mgr->mutex);
    mgr->tasks = g_list_append(mgr->tasks, task);
    g_mutex_unlock(&mgr->mutex);

    /* Send Offer: 13|||<id>|||<filename>|||<size> */
    char offer_cmd[512];
    snprintf(offer_cmd, sizeof(offer_cmd), "13|||%s|||%s|||%" PRIu64, task->id, task->file_name, task->file_size);
    send_utf16_cmd(mgr->session, offer_cmd);

    if (mgr->notice_cb) {
        char *msg = g_strdup_printf("Offering file to partner: %s (%" PRIu64 " bytes)", task->file_name, task->file_size);
        mgr->notice_cb(msg, mgr->callback_data);
        g_free(msg);
    }

    return true;
}

static char *get_unique_target_path(const char *file_name)
{
    const char *down_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!down_dir || !*down_dir) {
        down_dir = g_get_home_dir();
    }

    /* Strip dangerous path separators */
    char *clean_name = g_path_get_basename(file_name);
    char *candidate = g_build_filename(down_dir, clean_name, NULL);

    if (!g_file_test(candidate, G_FILE_TEST_EXISTS)) {
        g_free(clean_name);
        return candidate;
    }

    /* Handle collisions with (1), (2), etc. */
    char *dot = strrchr(clean_name, '.');
    char name_stem[256];
    char ext[64] = "";
    if (dot) {
        size_t stem_len = (size_t)(dot - clean_name);
        if (stem_len >= sizeof(name_stem)) stem_len = sizeof(name_stem) - 1;
        strncpy(name_stem, clean_name, stem_len);
        name_stem[stem_len] = '\0';
        g_strlcpy(ext, dot, sizeof(ext));
    } else {
        g_strlcpy(name_stem, clean_name, sizeof(name_stem));
    }
    g_free(clean_name);

    for (int i = 1; i < 1000; i++) {
        g_free(candidate);
        char suffixed[512];
        snprintf(suffixed, sizeof(suffixed), "%s (%d)%s", name_stem, i, ext);
        candidate = g_build_filename(down_dir, suffixed, NULL);
        if (!g_file_test(candidate, G_FILE_TEST_EXISTS)) {
            return candidate;
        }
    }

    return candidate;
}

void uv_ft_handle_parts(UvFileTransferManager *mgr, const GArray *parts)
{
    if (!mgr || !parts || parts->len < 2) return;

    const UvPart *p0 = &g_array_index(parts, UvPart, 0);
    if (p0->len < 1) return;
    uint8_t cmd = p0->data[0];

    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    char id[64];
    size_t id_len = p1->len < sizeof(id) - 1 ? p1->len : sizeof(id) - 1;
    memcpy(id, p1->data, id_len);
    id[id_len] = '\0';

    g_mutex_lock(&mgr->mutex);

    switch (cmd) {
    case 13: { /* Incoming Offer from host */
        if (parts->len < 4) break;
        const UvPart *p2 = &g_array_index(parts, UvPart, 2);
        const UvPart *p3 = &g_array_index(parts, UvPart, 3);

        char file_name[256];
        size_t name_len = p2->len < sizeof(file_name) - 1 ? p2->len : sizeof(file_name) - 1;
        memcpy(file_name, p2->data, name_len);
        file_name[name_len] = '\0';

        uint64_t file_size = 0;
        if (p3->len == 8) {
            double d = uv_read_f64_le(p3->data);
            if (d >= 0) file_size = (uint64_t)d;
        } else if (p3->len == 4) {
            file_size = (uint64_t)uv_read_u32_le(p3->data);
        }

        char *target_path = get_unique_target_path(file_name);
        FILE *fp = fopen(target_path, "wb");
        if (fp) {
            UvFileTransferTask *task = g_new0(UvFileTransferTask, 1);
            g_strlcpy(task->id, id, sizeof(task->id));
            g_strlcpy(task->file_name, file_name, sizeof(task->file_name));
            g_strlcpy(task->file_path, target_path, sizeof(task->file_path));
            task->file_size = file_size;
            task->offset = 0;
            task->fp = fp;
            task->is_outbound = false;
            mgr->tasks = g_list_append(mgr->tasks, task);

            /* Accept offer: 14|||<id>|||1 */
            char accept_cmd[128];
            snprintf(accept_cmd, sizeof(accept_cmd), "14|||%s|||1", id);
            send_utf16_cmd(mgr->session, accept_cmd);

            if (mgr->notice_cb) {
                char *msg = g_strdup_printf("Receiving file from partner: %s (%" PRIu64 " bytes)", file_name, file_size);
                mgr->notice_cb(msg, mgr->callback_data);
                g_free(msg);
            }
        }
        g_free(target_path);
        break;
    }
    case 14: { /* Accept from host (for our outbound offer) */
        for (GList *l = mgr->tasks; l; l = l->next) {
            UvFileTransferTask *task = (UvFileTransferTask *)l->data;
            if (task->is_outbound && strcmp(task->id, id) == 0) {
                send_next_chunk(mgr, task);
                break;
            }
        }
        break;
    }
    case 15: { /* Chunk from host */
        if (parts->len < 4) break;
        const UvPart *p2 = &g_array_index(parts, UvPart, 2);
        const UvPart *p3 = &g_array_index(parts, UvPart, 3);

        uint64_t offset_1_based = 1;
        if (p2->len == 8) {
            double d = uv_read_f64_le(p2->data);
            if (d >= 1) offset_1_based = (uint64_t)d;
        } else if (p2->len == 4) {
            offset_1_based = (uint64_t)uv_read_u32_le(p2->data);
        }

        for (GList *l = mgr->tasks; l; l = l->next) {
            UvFileTransferTask *task = (UvFileTransferTask *)l->data;
            if (!task->is_outbound && strcmp(task->id, id) == 0 && task->fp) {
                fseeko(task->fp, (off_t)(offset_1_based - 1), SEEK_SET);
                if (p3->len > 0) {
                    fwrite(p3->data, 1, p3->len, task->fp);
                }
                task->offset += p3->len;

                /* Send Ack: 21|||<id> */
                char ack_cmd[128];
                snprintf(ack_cmd, sizeof(ack_cmd), "21|||%s", id);
                send_utf16_cmd(mgr->session, ack_cmd);

                if (task->offset >= task->file_size) {
                    task->completed = true;
                    fclose(task->fp);
                    task->fp = NULL;
                    if (mgr->notice_cb) {
                        char *msg = g_strdup_printf("Received file saved to: %s", task->file_path);
                        mgr->notice_cb(msg, mgr->callback_data);
                        g_free(msg);
                    }
                }
                break;
            }
        }
        break;
    }
    case 21: { /* Ack from host */
        for (GList *l = mgr->tasks; l; l = l->next) {
            UvFileTransferTask *task = (UvFileTransferTask *)l->data;
            if (task->is_outbound && strcmp(task->id, id) == 0) {
                send_next_chunk(mgr, task);
                break;
            }
        }
        break;
    }
    case 16: { /* Cancel from host */
        for (GList *l = mgr->tasks; l; l = l->next) {
            UvFileTransferTask *task = (UvFileTransferTask *)l->data;
            if (strcmp(task->id, id) == 0) {
                task->cancelled = true;
                if (task->fp) {
                    fclose(task->fp);
                    task->fp = NULL;
                }
                if (mgr->notice_cb) {
                    char *msg = g_strdup_printf("File transfer cancelled: %s", task->file_name);
                    mgr->notice_cb(msg, mgr->callback_data);
                    g_free(msg);
                }
                break;
            }
        }
        break;
    }
    default:
        break;
    }

    g_mutex_unlock(&mgr->mutex);
}

void uv_ft_manager_free(UvFileTransferManager *mgr)
{
    if (!mgr) return;

    g_mutex_lock(&mgr->mutex);
    for (GList *l = mgr->tasks; l; l = l->next) {
        UvFileTransferTask *task = (UvFileTransferTask *)l->data;
        if (task->fp) {
            fclose(task->fp);
        }
        g_free(task);
    }
    g_list_free(mgr->tasks);
    mgr->tasks = NULL;
    g_mutex_unlock(&mgr->mutex);

    g_mutex_clear(&mgr->mutex);
    g_free(mgr);
}
