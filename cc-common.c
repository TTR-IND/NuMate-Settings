/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-common.c — shared UI primitives and GSettings resolution
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

#define CC_LOG_DOMAIN "cc-common"

#include "cc-common.h"

GtkWidget *
cc_page_new (GtkWidget **content_out)
{
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start  (vbox, CC_PAGE_MARGIN_H);
    gtk_widget_set_margin_end    (vbox, CC_PAGE_MARGIN_H);
    gtk_widget_set_margin_top    (vbox, CC_PAGE_MARGIN_TOP);
    gtk_widget_set_margin_bottom (vbox, CC_PAGE_MARGIN_BOT);
    gtk_container_add (GTK_CONTAINER (scroll), vbox);

    if (content_out) *content_out = vbox;
    return scroll;
}

GtkWidget *
cc_page_add_grid (GtkWidget *page)
{
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID (grid), CC_GRID_ROW_SPACING);
    gtk_grid_set_column_spacing (GTK_GRID (grid), CC_GRID_COL_SPACING);
    gtk_box_pack_start (GTK_BOX (page), grid, FALSE, TRUE, 0);
    return grid;
}

GtkWidget *
cc_section_card (void)
{
    GtkWidget *frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_style_context_add_class (gtk_widget_get_style_context (frame), "section-card");

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start  (box, 4);
    gtk_widget_set_margin_end    (box, 4);
    gtk_widget_set_margin_top    (box, 4);
    gtk_widget_set_margin_bottom (box, 4);
    gtk_container_add (GTK_CONTAINER (frame), box);

    /* Store the content box so callers can retrieve it if needed.
     * Typical use: pack the returned frame into the page, then
     * gtk_box_pack_* into the child box. */
    g_object_set_data (G_OBJECT (frame), "cc-content", box);
    return frame;
}

GtkWidget *
cc_section_card_content (GtkWidget *card)
{
    return g_object_get_data (G_OBJECT (card), "cc-content");
}

GtkWidget *
cc_row_new (const gchar *title, const gchar *subtitle, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start  (row, 8);
    gtk_widget_set_margin_end    (row, 8);
    gtk_widget_set_margin_top    (row, 10);
    gtk_widget_set_margin_bottom (row, 10);

    GtkWidget *text = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand (text, TRUE);
    gtk_widget_set_valign (text, GTK_ALIGN_CENTER);

    GtkWidget *title_l = gtk_label_new (title);
    gtk_label_set_xalign (GTK_LABEL (title_l), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (title_l), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand (title_l, TRUE);
    gtk_box_pack_start (GTK_BOX (text), title_l, FALSE, FALSE, 0);

    if (subtitle && *subtitle) {
        GtkWidget *sub = gtk_label_new (subtitle);
        gtk_label_set_xalign (GTK_LABEL (sub), 0.0);
        gtk_label_set_ellipsize (GTK_LABEL (sub), PANGO_ELLIPSIZE_END);
        gtk_style_context_add_class (gtk_widget_get_style_context (sub), "dim-label");
        gtk_box_pack_start (GTK_BOX (text), sub, FALSE, FALSE, 0);
    }

    gtk_box_pack_start (GTK_BOX (row), text, TRUE, TRUE, 0);

    if (control) {
        gtk_widget_set_valign (control, GTK_ALIGN_CENTER);
        gtk_widget_set_halign (control, GTK_ALIGN_END);
        gtk_box_pack_start (GTK_BOX (row), control, FALSE, FALSE, 0);
    }

    gtk_style_context_add_class (gtk_widget_get_style_context (row), "settings-row");
    return row;
}

GtkWidget *
cc_section_label (const gchar *text)
{
    GtkWidget *label = gtk_label_new (NULL);
    gchar *markup = g_markup_printf_escaped (
        "<span weight=\"bold\" size=\"small\">%s</span>", text);
    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);

    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_margin_top    (label, 18);
    gtk_widget_set_margin_bottom (label,  6);
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
    return label;
}

void
cc_grid_attach_row (GtkGrid *grid, gint row,
                    const gchar *caption, GtkWidget *widget)
{
    GtkWidget *label = gtk_label_new (caption);
    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end (label, CC_GRID_COL_SPACING);

    gtk_grid_attach (grid, label,  0, row, 1, 1);
    gtk_grid_attach (grid, widget, 1, row, 1, 1);
    gtk_widget_set_hexpand (widget, TRUE);
}

GtkWidget *
cc_marked_scale (gdouble min, gdouble max, gdouble step,
                 const gchar *low_mark, const gchar *high_mark)
{
    GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                 min, max, step);
    gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
    if (low_mark)  gtk_scale_add_mark (GTK_SCALE (scale), min, GTK_POS_BOTTOM, low_mark);
    if (high_mark) gtk_scale_add_mark (GTK_SCALE (scale), max, GTK_POS_BOTTOM, high_mark);
    return scale;
}

void
cc_grid_attach_bound_scale (GtkGrid *grid, gint row, const gchar *caption,
                            GSettings *settings, const gchar *key,
                            gdouble min, gdouble max, gdouble step,
                            const gchar *low_mark, const gchar *high_mark)
{
    GtkWidget *scale = cc_marked_scale (min, max, step, low_mark, high_mark);

    g_settings_bind (settings, key,
                     gtk_range_get_adjustment (GTK_RANGE (scale)), "value",
                     G_SETTINGS_BIND_DEFAULT);

    cc_grid_attach_row (grid, row, caption, scale);
}

GtkWidget *
cc_settings_switch (GSettings *settings, const gchar *key)
{
    GtkWidget *sw = gtk_switch_new ();
    gtk_widget_set_halign (sw, GTK_ALIGN_START);
    g_settings_bind (settings, key, sw, "active", G_SETTINGS_BIND_DEFAULT);
    return sw;
}

void
cc_closure_free_data (gpointer data, GClosure *closure G_GNUC_UNUSED)
{
    g_free (data);
}

gboolean
cc_schema_exists (const gchar *schema_id)
{
    GSettingsSchema *schema = g_settings_schema_source_lookup (
        g_settings_schema_source_get_default (), schema_id, TRUE);
    if (!schema) return FALSE;
    g_settings_schema_unref (schema);
    return TRUE;
}

GSettings *
cc_settings_resolve (const gchar *const *schema_ids, const gchar **chosen_out)
{
    if (chosen_out) *chosen_out = NULL;

    for (int i = 0; schema_ids[i]; i++) {
        if (!cc_schema_exists (schema_ids[i])) continue;
        if (chosen_out) *chosen_out = schema_ids[i];
        CC_LOG ("resolved schema '%s'", schema_ids[i]);
        return g_settings_new (schema_ids[i]);
    }

    CC_LOG ("no schema available from preference list starting '%s'",
            schema_ids[0] ? schema_ids[0] : "(empty)");
    return NULL;
}
