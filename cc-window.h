/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-window.h — main settings window
 *
 * Copyright (C) 2026 Josh Wheatstone TechnyxLabs
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

#ifndef CC_WINDOW_H
#define CC_WINDOW_H

#include <gtk/gtk.h>

#define CC_TYPE_WINDOW (cc_window_get_type ())
G_DECLARE_FINAL_TYPE (CcWindow, cc_window, CC, WINDOW, GtkApplicationWindow)

GtkWidget *cc_window_new (GtkApplication *app);

/* Open a panel by id, e.g. from the --panel command-line option. */
void cc_window_show_panel (CcWindow *self, const gchar *panel_id);

#endif /* CC_WINDOW_H */
