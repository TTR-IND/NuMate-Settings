/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-screensaver.c — Screen Lock & Screensaver settings panel
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
 * Screensaver theme discovery and preview mechanism adapted from
 * mate-screensaver src/mate-screensaver-preferences.c and gs-job.c:
 *   Copyright (C) 2004-2006 William Jon McCann <mccann@jhu.edu>
 *   Copyright (C) 2012-2021 MATE Developers
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/mate-desktop/mate-screensaver
 * -----------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for Josh Wheatstone, TechnyxLabs
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - stop_preview() removes the child watch before signalling the
 *     child. Upstream killed first, leaving the watch to fire on a
 *     reaped pid.
 *   - Saver id prefixing is centralised in saver_id_canonical().
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-screensaver"

#include <string.h>
#include <signal.h>
#include <gdk/gdkx.h>

#include "cc-common.h"
#include "cc-panels.h"

static const gchar *const SS_SCHEMAS[]  = { "org.mate.screensaver",
                                            "org.gnome.desktop.screensaver", NULL };
static const gchar *const SES_SCHEMAS[] = { "org.mate.session",
                                            "org.gnome.desktop.session", NULL };

#define MATE_SS_SCHEMA "org.mate.screensaver"

/* Present in both screensaver schemas */
#define KEY_LOCK       "lock-enabled"
#define KEY_LOCK_DELAY "lock-delay"
#define KEY_IDLE_DELAY "idle-delay"

/* org.mate.screensaver only */
#define KEY_IDLE_ACT "idle-activation-enabled"
#define KEY_MODE     "mode"
#define KEY_THEMES   "themes"

/* Saver ids stored in GSettings carry this prefix; .desktop basenames may
 * not. Every conversion goes through saver_id_canonical(). */
#define SAVER_PREFIX  "screensavers-"
#define SAVER_BLANK   "__blank-only"
#define SAVER_RANDOM  "__random"

#define SS_MODE_BLANK  0
#define SS_MODE_RANDOM 1
#define SS_MODE_SINGLE 2

static const gchar *const SAVER_DIRS[] = {
    "/usr/share/mate-screensavers",
    "/usr/share/applications/screensavers",
    "/usr/lib/xscreensaver",
    NULL
};

typedef struct {
    GSettings *ss_s;
    GSettings *ses_s;
    gboolean   has_mate_ss;   /* mode/themes/idle-activation keys exist */

    GtkWidget *saver_list;
    GtkWidget *preview_area;

    GPid  preview_pid;
    guint preview_watch;

    gboolean updating;
} SSData;

/* ================================================================== */
/*  Discovery                                                           */
/* ================================================================== */

typedef struct {
    gchar *id;
    gchar *name;
    gchar *exec;
} SaverInfo;

static void
saver_info_free (SaverInfo *s)
{
    if (!s) return;
    g_free (s->id);
    g_free (s->name);
    g_free (s->exec);
    g_free (s);
}

static gint
saver_info_cmp (gconstpointer a, gconstpointer b)
{
    return g_strcmp0 (((const SaverInfo *) a)->name, ((const SaverInfo *) b)->name);
}

/* GSettings stores "screensavers-NAME"; add the prefix when missing.
 * The two special ids are passed through untouched. */
static gchar *
saver_id_canonical (const gchar *id)
{
    if (g_str_has_prefix (id, "__") || g_str_has_prefix (id, SAVER_PREFIX))
        return g_strdup (id);

    return g_strconcat (SAVER_PREFIX, id, NULL);
}

static GList *
discover_savers (void)
{
    GList *list = NULL;
    GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);

    for (int di = 0; SAVER_DIRS[di]; di++) {
        GDir *dir = g_dir_open (SAVER_DIRS[di], 0, NULL);
        if (!dir) continue;

        const gchar *fname;
        while ((fname = g_dir_read_name (dir))) {
            if (!g_str_has_suffix (fname, ".desktop")) continue;

            gchar *path = g_build_filename (SAVER_DIRS[di], fname, NULL);
            GKeyFile *kf = g_key_file_new ();

            if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
                gchar *id   = g_strndup (fname, strlen (fname) - strlen (".desktop"));
                gchar *name = g_key_file_get_locale_string (kf, "Desktop Entry", "Name", NULL, NULL);
                gchar *exec = g_key_file_get_string (kf, "Desktop Entry", "Exec", NULL);

                if (name && exec && !g_hash_table_contains (seen, id)) {
                    SaverInfo *si = g_new0 (SaverInfo, 1);
                    si->id   = id;
                    si->name = name;
                    si->exec = exec;
                    g_hash_table_add (seen, si->id);
                    list = g_list_prepend (list, si);
                } else {
                    g_free (id);
                    g_free (name);
                    g_free (exec);
                }
            }

            g_key_file_free (kf);
            g_free (path);
        }
        g_dir_close (dir);
    }

    g_hash_table_destroy (seen);
    return g_list_sort (list, saver_info_cmp);
}

/* ================================================================== */
/*  Preview                                                             */
/* ================================================================== */

static void
stop_preview (SSData *d)
{
    /* Drop the watch first: otherwise the SIGTERM reaps into a callback
     * that no longer has a live pid to close. */
    if (d->preview_watch) {
        g_source_remove (d->preview_watch);
        d->preview_watch = 0;
    }

    if (d->preview_pid > 0) {
        kill (d->preview_pid, SIGTERM);
        g_spawn_close_pid (d->preview_pid);
        d->preview_pid = 0;
    }
}

static void
on_preview_died (GPid pid, gint status G_GNUC_UNUSED, gpointer user_data)
{
    SSData *d = user_data;

    g_spawn_close_pid (pid);
    if (d->preview_pid == pid) {
        d->preview_pid   = 0;
        d->preview_watch = 0;
    }
}

static void
start_preview (SSData *d, const gchar *exec)
{
    stop_preview (d);

    if (!exec || !*exec) return;
    if (!gtk_widget_get_realized (d->preview_area)) return;

    GdkWindow *win = gtk_widget_get_window (d->preview_area);
    if (!win) return;

    gchar **argv = NULL;
    GError *err  = NULL;

    if (!g_shell_parse_argv (exec, NULL, &argv, &err)) {
        g_warning ("cc-screensaver: bad Exec '%s': %s", exec, err->message);
        g_clear_error (&err);
        return;
    }

    gchar *xid_str = g_strdup_printf ("0x%X", (guint32) GDK_WINDOW_XID (win));
    gchar **env = g_environ_setenv (g_get_environ (), "XSCREENSAVER_WINDOW", xid_str, TRUE);
    env = g_environ_setenv (env, "DISPLAY",
                            gdk_display_get_name (gdk_window_get_display (win)), TRUE);
    g_free (xid_str);

    GPid pid;
    if (g_spawn_async (NULL, argv, env,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, &pid, &err)) {
        d->preview_pid   = pid;
        d->preview_watch = g_child_watch_add (pid, on_preview_died, d);
    } else {
        g_warning ("cc-screensaver: cannot start preview: %s", err->message);
        g_clear_error (&err);
    }

    g_strfreev (argv);
    g_strfreev (env);
}

static void
on_preview_mapped (GtkWidget *widget, gpointer user_data)
{
    const gchar *exec = g_object_get_data (G_OBJECT (widget), "initial-exec");
    if (exec) start_preview (user_data, exec);
}

/* ================================================================== */
/*  GSettings read/write                                                */
/* ================================================================== */

static gchar *
get_current_saver_id (SSData *d)
{
    if (!d->ss_s || !d->has_mate_ss) return NULL;

    switch (g_settings_get_enum (d->ss_s, KEY_MODE)) {
    case SS_MODE_BLANK:  return g_strdup (SAVER_BLANK);
    case SS_MODE_RANDOM: return g_strdup (SAVER_RANDOM);
    default: break;
    }

    gchar **themes = g_settings_get_strv (d->ss_s, KEY_THEMES);
    gchar *id = (themes && themes[0]) ? g_strdup (themes[0]) : NULL;
    g_strfreev (themes);
    return id;
}

static void
set_saver_id (SSData *d, const gchar *id)
{
    if (!d->ss_s || !d->has_mate_ss || !id) return;

    gchar *full_id = saver_id_canonical (id);
    CC_LOG ("saver '%s' stored as '%s'", id, full_id);

    if (!g_strcmp0 (full_id, SAVER_BLANK)) {
        g_settings_set_enum (d->ss_s, KEY_MODE, SS_MODE_BLANK);
    } else if (!g_strcmp0 (full_id, SAVER_RANDOM)) {
        g_settings_set_enum (d->ss_s, KEY_MODE, SS_MODE_RANDOM);
    } else {
        const gchar *themes[] = { full_id, NULL };
        g_settings_set_enum (d->ss_s, KEY_MODE, SS_MODE_SINGLE);
        g_settings_set_strv (d->ss_s, KEY_THEMES, themes);
    }

    g_free (full_id);
}

static void
on_saver_selected (GtkListBox *lb G_GNUC_UNUSED, GtkListBoxRow *row, SSData *d)
{
    if (!row || d->updating) return;

    set_saver_id (d, g_object_get_data (G_OBJECT (row), "saver-id"));
    start_preview (d, g_object_get_data (G_OBJECT (row), "saver-exec"));
}

/* ================================================================== */
/*  Idle scale (minutes in the UI, seconds in GSettings)                */
/* ================================================================== */

static gchar *
format_minutes (GtkScale *s G_GNUC_UNUSED, gdouble v, gpointer u G_GNUC_UNUSED)
{
    if (v < 1)  return g_strdup (_("Never"));
    if (v < 60) return g_strdup_printf (_("%d min"), (int) v);
    return g_strdup_printf (_("%d hr"), (int) (v / 60));
}

static void
on_idle_changed (GtkRange *range, SSData *d)
{
    if (d->updating || !d->ses_s) return;

    gint minutes = (gint) gtk_range_get_value (range);
    g_settings_set_int (d->ses_s, KEY_IDLE_DELAY, minutes * 60);
}

/* ================================================================== */
/*  Teardown                                                            */
/* ================================================================== */

static void
ss_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, SSData *d)
{
    stop_preview (d);
    g_clear_object (&d->ss_s);
    g_clear_object (&d->ses_s);
    g_free (d);
}

/* ================================================================== */
/*  Construction helpers                                                */
/* ================================================================== */

static GtkWidget *
make_saver_row (const gchar *label, const gchar *id, const gchar *exec)
{
    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *lbl = gtk_label_new (label);

    gtk_widget_set_halign (lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start  (lbl, 8);
    gtk_widget_set_margin_end    (lbl, 8);
    gtk_widget_set_margin_top    (lbl, 6);
    gtk_widget_set_margin_bottom (lbl, 6);
    gtk_container_add (GTK_CONTAINER (row), lbl);

    g_object_set_data_full (G_OBJECT (row), "saver-id", g_strdup (id), g_free);
    if (exec)
        g_object_set_data_full (G_OBJECT (row), "saver-exec", g_strdup (exec), g_free);

    return row;
}

static void
select_current_saver (SSData *d)
{
    gchar *current = get_current_saver_id (d);
    if (!current) return;

    d->updating = TRUE;

    GList *rows = gtk_container_get_children (GTK_CONTAINER (d->saver_list));
    for (GList *l = rows; l; l = l->next) {
        if (!GTK_IS_LIST_BOX_ROW (l->data)) continue;

        const gchar *id = g_object_get_data (G_OBJECT (l->data), "saver-id");
        if (!id) continue;

        gchar *canonical = saver_id_canonical (id);
        gboolean match = (g_strcmp0 (canonical, current) == 0);
        g_free (canonical);
        if (!match) continue;

        gtk_list_box_select_row (GTK_LIST_BOX (d->saver_list),
                                 GTK_LIST_BOX_ROW (l->data));

        /* The preview needs a realised X window, so the command is parked
         * on the drawing area and launched from its "map" handler. */
        const gchar *exec = g_object_get_data (G_OBJECT (l->data), "saver-exec");
        if (exec)
            g_object_set_data_full (G_OBJECT (d->preview_area), "initial-exec",
                                    g_strdup (exec), g_free);
        break;
    }

    g_list_free (rows);
    g_free (current);
    d->updating = FALSE;
}

/* ================================================================== */
/*  Public                                                              */
/* ================================================================== */

GtkWidget *
cc_screensaver_panel_new (void)
{
    SSData *d = g_new0 (SSData, 1);
    const gchar *ss_schema = NULL;

    d->ss_s        = cc_settings_resolve (SS_SCHEMAS, &ss_schema);
    d->ses_s       = cc_settings_resolve (SES_SCHEMAS, NULL);
    d->has_mate_ss = (g_strcmp0 (ss_schema, MATE_SS_SCHEMA) == 0);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (ss_panel_destroyed), d);

    /* ---- Saver list + preview ----------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("SCREENSAVER")),
                        FALSE, FALSE, 0);

    GtkWidget *ss_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start (GTK_BOX (page), ss_hbox, FALSE, TRUE, 0);

    GtkWidget *list_sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (list_sw),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (list_sw), GTK_SHADOW_IN);
    gtk_widget_set_size_request (list_sw, 220, 280);

    d->saver_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (d->saver_list), GTK_SELECTION_SINGLE);
    gtk_container_add (GTK_CONTAINER (list_sw), d->saver_list);
    gtk_box_pack_start (GTK_BOX (ss_hbox), list_sw, FALSE, TRUE, 0);

    GtkWidget *preview_frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (preview_frame), GTK_SHADOW_IN);
    d->preview_area = gtk_drawing_area_new ();
    gtk_widget_set_size_request (d->preview_area, 320, 180);

    GtkCssProvider *black = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (black, "* { background-color: black; }", -1, NULL);
    gtk_style_context_add_provider (gtk_widget_get_style_context (d->preview_area),
                                    GTK_STYLE_PROVIDER (black),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (black);

    gtk_container_add (GTK_CONTAINER (preview_frame), d->preview_area);
    gtk_box_pack_start (GTK_BOX (ss_hbox), preview_frame, TRUE, TRUE, 0);
    g_signal_connect (d->preview_area, "map", G_CALLBACK (on_preview_mapped), d);

    gtk_list_box_insert (GTK_LIST_BOX (d->saver_list),
                         make_saver_row (_("Blank screen"), SAVER_BLANK, NULL), -1);
    gtk_list_box_insert (GTK_LIST_BOX (d->saver_list),
                         make_saver_row (_("Random"), SAVER_RANDOM, NULL), -1);
    gtk_list_box_insert (GTK_LIST_BOX (d->saver_list),
                         gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), -1);

    GList *savers = discover_savers ();
    CC_LOG ("discovered %u savers", g_list_length (savers));
    for (GList *l = savers; l; l = l->next) {
        SaverInfo *si = l->data;
        gtk_list_box_insert (GTK_LIST_BOX (d->saver_list),
                             make_saver_row (si->name, si->id, si->exec), -1);
    }
    g_list_free_full (savers, (GDestroyNotify) saver_info_free);

    select_current_saver (d);
    g_signal_connect (d->saver_list, "row-activated", G_CALLBACK (on_saver_selected), d);

    /* ---- Screen lock -------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("SCREEN LOCK")),
                        FALSE, FALSE, 0);
    GtkGrid *lg = GTK_GRID (cc_page_add_grid (page));

    if (d->ss_s) {
        cc_grid_attach_row (lg, 0, _("Lock screen"),
                            cc_settings_switch (d->ss_s, KEY_LOCK));

        if (d->has_mate_ss)
            cc_grid_attach_row (lg, 1, _("Activate when idle"),
                                cc_settings_switch (d->ss_s, KEY_IDLE_ACT));
    }

    /* ---- Timing ------------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("TIMING")), FALSE, FALSE, 0);
    GtkGrid *tg = GTK_GRID (cc_page_add_grid (page));

    GtkWidget *idle_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 1, 120, 1);
    gtk_scale_set_draw_value (GTK_SCALE (idle_scale), TRUE);
    gtk_scale_set_value_pos  (GTK_SCALE (idle_scale), GTK_POS_RIGHT);
    gtk_scale_add_mark (GTK_SCALE (idle_scale),   1, GTK_POS_BOTTOM, _("1 min"));
    gtk_scale_add_mark (GTK_SCALE (idle_scale),  60, GTK_POS_BOTTOM, _("1 hr"));
    gtk_scale_add_mark (GTK_SCALE (idle_scale), 120, GTK_POS_BOTTOM, _("2 hr"));
    g_signal_connect (idle_scale, "format-value", G_CALLBACK (format_minutes), NULL);

    if (d->ses_s) {
        gint delay = g_settings_get_int (d->ses_s, KEY_IDLE_DELAY);
        d->updating = TRUE;
        gtk_range_set_value (GTK_RANGE (idle_scale), delay < 60 ? 1 : delay / 60);
        d->updating = FALSE;
        g_signal_connect (idle_scale, "value-changed", G_CALLBACK (on_idle_changed), d);
    } else {
        gtk_widget_set_sensitive (idle_scale, FALSE);
    }
    cc_grid_attach_row (tg, 0, _("Activate after"), idle_scale);

    GtkWidget *lock_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 3600, 30);
    gtk_scale_set_draw_value (GTK_SCALE (lock_scale), FALSE);
    gtk_scale_add_mark (GTK_SCALE (lock_scale),    0, GTK_POS_BOTTOM, _("Immediately"));
    gtk_scale_add_mark (GTK_SCALE (lock_scale), 1800, GTK_POS_BOTTOM, _("30 min"));
    gtk_scale_add_mark (GTK_SCALE (lock_scale), 3600, GTK_POS_BOTTOM, _("1 hr"));

    if (d->ss_s) {
        /* lock-delay is already in seconds, so the adjustment binds direct. */
        g_settings_bind (d->ss_s, KEY_LOCK_DELAY,
                         gtk_range_get_adjustment (GTK_RANGE (lock_scale)),
                         "value", G_SETTINGS_BIND_DEFAULT);
    } else {
        gtk_widget_set_sensitive (lock_scale, FALSE);
    }
    cc_grid_attach_row (tg, 1, _("Lock after activating"), lock_scale);

    gtk_widget_show_all (scroll);
    return scroll;
}
