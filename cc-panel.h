/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-panel.h — settings panel descriptor
 *
 * Copyright (C) 2026 Josh A. Wheatstone - Torfaen Technology Research IND.
 * Copyright (C) 2025 NuMate desktop contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Raw C translation of the ukui-control-center application shell,
 * written by J. A. Wheatstone. The original is C++/Qt:
 *   Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/ukui/ukui-control-center
 */

#ifndef CC_PANEL_H
#define CC_PANEL_H

#include <gtk/gtk.h>

typedef GtkWidget *(*CcPanelCreateFunc) (void);

/*
 * Exactly one of `create` and `exec` is set: built-in panels supply a
 * constructor, external capplets supply a command line to embed.
 */
typedef struct {
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *icon_name;
    gchar *exec;
    gchar *category;
    gint   order;
    CcPanelCreateFunc create;
} CcPanel;

void       cc_panel_free      (CcPanel *panel);
GPtrArray *cc_panels_discover (void);

#endif /* CC_PANEL_H */
