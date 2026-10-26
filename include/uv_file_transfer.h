/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - File Transfer
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_FILE_TRANSFER_H
#define UV_FILE_TRANSFER_H

#include "uv_types.h"
#include "uv_wire.h"

typedef struct _UvSession UvSession;
typedef struct UvFileTransferManager UvFileTransferManager;

typedef void (*UvFileTransferNoticeCb)(const char *message, gpointer user_data);

typedef struct {
    char id[32];
    char file_name[256];
    char file_path[1024];
    uint64_t file_size;
    uint64_t offset;
    FILE *fp;
    bool is_outbound;
    bool completed;
    bool cancelled;
} UvFileTransferTask;

UvFileTransferManager *uv_ft_manager_new(UvSession *session, UvFileTransferNoticeCb notice_cb, gpointer callback_data);
bool uv_ft_send_file(UvFileTransferManager *mgr, const char *local_path);
void uv_ft_handle_parts(UvFileTransferManager *mgr, const GArray *parts);
void uv_ft_manager_free(UvFileTransferManager *mgr);

#endif /* UV_FILE_TRANSFER_H */
