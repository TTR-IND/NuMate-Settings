/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-panels.h — constructors for every built-in settings panel
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
 *
 * One header for all twelve panels rather than twelve one-line headers:
 * the signature is identical for every panel, so there is exactly one
 * place where that contract is written down.
 *
 * Contract: each function returns a floating GtkWidget owning all of its
 * panel state. The state is released from the widget's "destroy" handler,
 * so the caller only has to parent the widget.
 */

#ifndef CC_PANELS_H
#define CC_PANELS_H

#include <gtk/gtk.h>

GtkWidget *cc_background_panel_new  (void);
GtkWidget *cc_themes_panel_new      (void);
GtkWidget *cc_screensaver_panel_new (void);
GtkWidget *cc_display_panel_new     (void);
GtkWidget *cc_sound_panel_new       (void);
GtkWidget *cc_keyboard_panel_new    (void);
GtkWidget *cc_mouse_panel_new       (void);
GtkWidget *cc_power_panel_new       (void);
GtkWidget *cc_datetime_panel_new    (void);
GtkWidget *cc_users_panel_new       (void);
GtkWidget *cc_defaultapps_panel_new (void);
GtkWidget *cc_network_panel_new     (void);

#endif /* CC_PANELS_H */
