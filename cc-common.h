/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-common.h — shared UI primitives and GSettings resolution
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

#ifndef CC_COMMON_H
#define CC_COMMON_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

/*
 * Each translation unit defines CC_LOG_DOMAIN before including this header.
 * The guard below only ever executes in units that did not, so the default
 * cannot silently override a panel's own domain.
 */
#ifndef CC_LOG_DOMAIN
#define CC_LOG_DOMAIN "numate-settings"
#endif

#define CC_LOG(fmt, ...) g_log (CC_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

/* Single definition of the panel chrome metrics. Every panel references
 * these; there is no second copy anywhere in the tree. */
#define CC_PAGE_MARGIN_H   28
#define CC_PAGE_MARGIN_TOP 8
#define CC_PAGE_MARGIN_BOT 28
#define CC_GRID_ROW_SPACING 6
#define CC_GRID_COL_SPACING 12

/* Root scroller + content box used by every panel. Returns the scroller;
 * *content_out receives the vertical box callers pack into. */
GtkWidget *cc_page_new (GtkWidget **content_out);

/* A grid pre-configured with the standard spacing, packed into `page`. */
GtkWidget *cc_page_add_grid (GtkWidget *page);

GtkWidget *cc_section_card (void);
GtkWidget *cc_section_card_content (GtkWidget *card);
GtkWidget *cc_section_label (const gchar *text);

/* ChromeOS/Apple-style settings row: title (+ optional subtitle) left, control right. */
GtkWidget *cc_row_new (const gchar *title, const gchar *subtitle, GtkWidget *control);

/* Attach `widget` at `row` with a right-aligned caption in column 0. */
void cc_grid_attach_row (GtkGrid *grid, gint row,
                         const gchar *caption, GtkWidget *widget);

/* Horizontal scale with optional end marks and no inline value. */
GtkWidget *cc_marked_scale (gdouble min, gdouble max, gdouble step,
                            const gchar *low_mark, const gchar *high_mark);

/*
 * Marked scale bound to a numeric key and attached with its caption.
 * Three panels needed the identical eight-line sequence; it is defined
 * once here rather than copied per panel.
 */
void cc_grid_attach_bound_scale (GtkGrid *grid, gint row, const gchar *caption,
                                 GSettings *settings, const gchar *key,
                                 gdouble min, gdouble max, gdouble step,
                                 const gchar *low_mark, const gchar *high_mark);

/* GtkSwitch two-way bound to a boolean key. */
GtkWidget *cc_settings_switch (GSettings *settings, const gchar *key);

/*
 * Schema resolution. `schema_ids` is a NULL-terminated preference list;
 * the first installed schema wins. Returns NULL when none are present so
 * callers can degrade rather than abort — a missing schema is an
 * environmental error, not a contract violation.
 */
/*
 * GDestroyNotify for closure user data.
 *
 * g_free has the wrong signature for GClosureNotify, and casting between
 * incompatible function pointer types is undefined behaviour even where
 * it happens to work. Three panels attach heap-allocated bindings to
 * closures; they all use this one correctly-typed wrapper.
 */
void cc_closure_free_data (gpointer data, GClosure *closure);

gboolean   cc_schema_exists    (const gchar *schema_id);
GSettings *cc_settings_resolve (const gchar *const *schema_ids,
                                const gchar **chosen_out);

#endif /* CC_COMMON_H */
