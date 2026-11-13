/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-defaultapps.c — Default Applications settings panel
 *
 * Copyright (C) 2026 Josh Wheatstone TechnyxLabs
 * Copyright (C) 2025 MATE Developers (capplet code)
 * Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd. (layout)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * -----------------------------------------------------------------------
 * Backend: GAppInfo — reads and writes MIME default applications.
 * Layout ported from the UKUI defaultapp plugin:
 *   Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/ukui/ukui-control-center
 * -----------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for Josh Wheatstone, TechnyxLabs
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - The "Reset to defaults" button is relabelled "Reload from system";
 *     it only repopulates the combos from the current MIME defaults.
 *   - A default registered outside a type's app list is prepended so
 *     that it stays selectable.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-defaultapps"

#include <gio/gdesktopappinfo.h>

#include "cc-common.h"
#include "cc-panels.h"

/*
 * The MIME strings are string literals with static storage duration, so
 * they can be handed to signal handlers as user data without a copy.
 */
static const struct { const gchar *label; const gchar *mime; } APP_ROWS[] = {
    { N_("Web browser"),  "x-scheme-handler/http"   },
    { N_("Email client"), "x-scheme-handler/mailto" },
    { N_("Image viewer"), "image/png"               },
    { N_("Music player"), "audio/x-vorbis+ogg"      },
    { N_("Video player"), "video/x-ogm+ogg"         },
    { N_("Text editor"),  "text/plain"              },
    { N_("File manager"), "inode/directory"         },
};

typedef struct {
    GtkWidget *combos[G_N_ELEMENTS (APP_ROWS)];
} DefaultAppsData;

static void
populate_combo_for_mime (GtkComboBoxText *combo, const gchar *mime_type)
{
    gtk_combo_box_text_remove_all (combo);

    GList *apps = g_app_info_get_all_for_type (mime_type);
    GAppInfo *def = g_app_info_get_default_for_type (mime_type, FALSE);
    const gchar *def_id = def ? g_app_info_get_id (def) : NULL;
    gboolean def_listed = FALSE;

    for (GList *l = apps; l; l = l->next) {
        GAppInfo *ai = G_APP_INFO (l->data);
        const gchar *id = g_app_info_get_id (ai);

        gtk_combo_box_text_append (combo, id, g_app_info_get_name (ai));
        if (g_strcmp0 (id, def_id) == 0) def_listed = TRUE;
    }

    /* A default registered outside the type's app list still has to be
     * selectable, or opening the combo would silently change it. */
    if (def && !def_listed)
        gtk_combo_box_text_prepend (combo, def_id, g_app_info_get_name (def));

    if (def_id)
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), def_id);
    else if (apps)
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

    g_list_free_full (apps, g_object_unref);
    if (def) g_object_unref (def);
}

static void
on_app_changed (GtkComboBox *cb, gpointer mime_type)
{
    const gchar *app_id = gtk_combo_box_get_active_id (cb);
    if (!app_id) return;

    GDesktopAppInfo *info = g_desktop_app_info_new (app_id);
    if (!info) {
        g_warning ("cc-defaultapps: no desktop entry for '%s'", app_id);
        return;
    }

    GError *err = NULL;
    g_app_info_set_as_default_for_type (G_APP_INFO (info), mime_type, &err);
    if (err) {
        g_warning ("cc-defaultapps: cannot set default for %s: %s",
                   (const gchar *) mime_type, err->message);
        g_clear_error (&err);
    }

    g_object_unref (info);
}

static void
on_reset_clicked (GtkButton *btn G_GNUC_UNUSED, DefaultAppsData *d)
{
    for (gsize i = 0; i < G_N_ELEMENTS (APP_ROWS); i++)
        populate_combo_for_mime (GTK_COMBO_BOX_TEXT (d->combos[i]), APP_ROWS[i].mime);
}

static void
defaultapps_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, DefaultAppsData *d)
{
    g_free (d);
}

GtkWidget *
cc_defaultapps_panel_new (void)
{
    DefaultAppsData *d = g_new0 (DefaultAppsData, 1);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (defaultapps_panel_destroyed), d);

    GtkWidget *card = cc_section_card ();
    GtkWidget *content = cc_section_card_content (card);

    for (gsize i = 0; i < G_N_ELEMENTS (APP_ROWS); i++) {
        d->combos[i] = gtk_combo_box_text_new ();
        populate_combo_for_mime (GTK_COMBO_BOX_TEXT (d->combos[i]), APP_ROWS[i].mime);
        g_signal_connect (d->combos[i], "changed", G_CALLBACK (on_app_changed),
                          (gpointer) APP_ROWS[i].mime);
        gtk_box_pack_start (GTK_BOX (content),
            cc_row_new (_(APP_ROWS[i].label), NULL, d->combos[i]),
            FALSE, FALSE, 0);
    }
    gtk_box_pack_start (GTK_BOX (page), card, FALSE, FALSE, 0);

    GtkWidget *reset_btn = gtk_button_new_with_label (_("Reload from system"));
    gtk_widget_set_halign (reset_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_top (reset_btn, 16);
    gtk_widget_set_margin_start (reset_btn, 4);
    g_signal_connect (reset_btn, "clicked", G_CALLBACK (on_reset_clicked), d);
    gtk_box_pack_start (GTK_BOX (page), reset_btn, FALSE, FALSE, 0);

    gtk_widget_show_all (scroll);
    return scroll;
}
