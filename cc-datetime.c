/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-datetime.c — Date & Time settings panel
 *
 * Copyright (C) 2026 Josh A. Wheatstone - Torfaen Technology Research IND.
 * Copyright (C) 2025 MATE Developers (capplet code)
 * Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd. (layout)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * -----------------------------------------------------------------------
 * Backend: org.freedesktop.timedate1 DBus (timedatectl)
 * Layout ported from the UKUI datetime plugin:
 *   Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/ukui/ukui-control-center
 * -----------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for J. A. Wheatstone, Torfaen Technology
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - localtime() replaced with localtime_r().
 *   - Timezone rows own a copy of the name returned by
 *     g_dir_read_name().
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-datetime"

#include <string.h>
#include <time.h>
#include <gio/gio.h>

#include "cc-common.h"
#include "cc-panels.h"

#define TIMEDATE_BUS   "org.freedesktop.timedate1"
#define TIMEDATE_PATH  "/org/freedesktop/timedate1"
#define TIMEDATE_IFACE "org.freedesktop.timedate1"

#define ZONEINFO_DIR "/usr/share/zoneinfo"

typedef struct {
    GDBusProxy *td_proxy;

    GtkWidget *clock_label;
    GtkWidget *date_label;
    GtkWidget *timezone_label;
    GtkWidget *ntp_sw;
    GtkWidget *tz_btn;

    guint clock_tick_id;
} DateTimeData;

/* ------------------------------------------------------------------ */
/*  Clock                                                               */
/* ------------------------------------------------------------------ */

static gboolean
update_clock (gpointer user_data)
{
    DateTimeData *d = user_data;
    time_t now = time (NULL);
    struct tm tm;
    gchar buf[64];

    localtime_r (&now, &tm);

    strftime (buf, sizeof buf, "%H:%M:%S", &tm);
    gtk_label_set_text (GTK_LABEL (d->clock_label), buf);

    strftime (buf, sizeof buf, "%A, %e %B %Y", &tm);
    gtk_label_set_text (GTK_LABEL (d->date_label), buf);

    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/*  timedated                                                           */
/* ------------------------------------------------------------------ */

static void
load_timedate_info (DateTimeData *d)
{
    if (!d->td_proxy) return;

    GVariant *v = g_dbus_proxy_get_cached_property (d->td_proxy, "Timezone");
    if (v) {
        gtk_label_set_text (GTK_LABEL (d->timezone_label),
                            g_variant_get_string (v, NULL));
        g_variant_unref (v);
    }

    v = g_dbus_proxy_get_cached_property (d->td_proxy, "NTP");
    if (v) {
        gtk_switch_set_active (GTK_SWITCH (d->ntp_sw), g_variant_get_boolean (v));
        g_variant_unref (v);
    }
}

static void
on_td_properties_changed (GDBusProxy *p G_GNUC_UNUSED,
                          GVariant *changed G_GNUC_UNUSED,
                          GStrv invalidated G_GNUC_UNUSED,
                          DateTimeData *d)
{
    load_timedate_info (d);
}

static gboolean
on_ntp_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, DateTimeData *d)
{
    if (!d->td_proxy) return FALSE;

    g_dbus_proxy_call (d->td_proxy, "SetNTP",
                       g_variant_new ("(bb)", state, FALSE),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Timezone picker                                                     */
/* ------------------------------------------------------------------ */

static void
add_tz_row (GtkListBox *listbox, const gchar *tz)
{
    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *label = gtk_label_new (tz);

    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_margin_start (label, 8);
    gtk_container_add (GTK_CONTAINER (row), label);

    /* g_dir_read_name's buffer is invalidated by the next call, so the
     * row must own a copy of the name. */
    g_object_set_data_full (G_OBJECT (row), "tz", g_strdup (tz), g_free);
    gtk_list_box_insert (listbox, row, -1);
}

static void
populate_timezones (GtkListBox *listbox)
{
    GDir *dir = g_dir_open (ZONEINFO_DIR, 0, NULL);
    if (!dir) {
        g_warning ("cc-datetime: cannot read %s", ZONEINFO_DIR);
        return;
    }

    const gchar *name;
    while ((name = g_dir_read_name (dir))) {
        gchar *path = g_build_filename (ZONEINFO_DIR, name, NULL);

        if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
            GDir *subdir = g_dir_open (path, 0, NULL);
            if (subdir) {
                const gchar *sub;
                while ((sub = g_dir_read_name (subdir))) {
                    gchar *tz = g_strdup_printf ("%s/%s", name, sub);
                    add_tz_row (listbox, tz);
                    g_free (tz);
                }
                g_dir_close (subdir);
            }
        } else if (!strchr (name, '.')) {
            add_tz_row (listbox, name);
        }

        g_free (path);
    }

    g_dir_close (dir);
}

static void
on_tz_clicked (GtkButton *btn G_GNUC_UNUSED, DateTimeData *d)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons (
        _("Select Timezone"),
        GTK_WINDOW (gtk_widget_get_toplevel (d->tz_btn)),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Apply"),  GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size (GTK_WINDOW (dlg), 400, 500);

    GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dlg));

    GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_vexpand (sw, TRUE);
    GtkWidget *listbox = gtk_list_box_new ();
    gtk_container_add (GTK_CONTAINER (sw), listbox);
    gtk_box_pack_start (GTK_BOX (content), sw, TRUE, TRUE, 0);

    populate_timezones (GTK_LIST_BOX (listbox));
    gtk_widget_show_all (content);

    if (gtk_dialog_run (GTK_DIALOG (dlg)) == GTK_RESPONSE_ACCEPT) {
        GtkListBoxRow *sel = gtk_list_box_get_selected_row (GTK_LIST_BOX (listbox));
        const gchar *tz = sel ? g_object_get_data (G_OBJECT (sel), "tz") : NULL;

        if (tz && d->td_proxy) {
            CC_LOG ("setting timezone '%s'", tz);
            g_dbus_proxy_call (d->td_proxy, "SetTimezone",
                               g_variant_new ("(sb)", tz, FALSE),
                               G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        }
    }

    gtk_widget_destroy (dlg);
}

/* ------------------------------------------------------------------ */
/*  Teardown                                                            */
/* ------------------------------------------------------------------ */

static void
datetime_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, DateTimeData *d)
{
    if (d->clock_tick_id) g_source_remove (d->clock_tick_id);
    g_clear_object (&d->td_proxy);
    g_free (d);
}

/* ------------------------------------------------------------------ */
/*  Public                                                              */
/* ------------------------------------------------------------------ */

static GtkWidget *
make_labelled_row (const gchar *caption, GtkWidget *value, GtkWidget *trailing)
{
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new (caption);

    gtk_widget_set_halign (label, GTK_ALIGN_END);
    gtk_label_set_width_chars (GTK_LABEL (label), 14);

    gtk_box_pack_start (GTK_BOX (row), label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row), value, TRUE, TRUE, 0);
    if (trailing)
        gtk_box_pack_start (GTK_BOX (row), trailing, FALSE, FALSE, 0);

    return row;
}

GtkWidget *
cc_datetime_panel_new (void)
{
    DateTimeData *d = g_new0 (DateTimeData, 1);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (datetime_panel_destroyed), d);

    d->clock_label = gtk_label_new ("00:00:00");
    gtk_style_context_add_class (gtk_widget_get_style_context (d->clock_label), "title-1");
    gtk_widget_set_margin_top    (d->clock_label, 8);
    gtk_widget_set_margin_bottom (d->clock_label, 4);
    gtk_box_pack_start (GTK_BOX (page), d->clock_label, FALSE, FALSE, 0);

    d->date_label = gtk_label_new ("");
    gtk_style_context_add_class (gtk_widget_get_style_context (d->date_label), "dim-label");
    gtk_widget_set_margin_bottom (d->date_label, 16);
    gtk_box_pack_start (GTK_BOX (page), d->date_label, FALSE, FALSE, 0);

    d->timezone_label = gtk_label_new (_("Unknown"));
    gtk_widget_set_halign (d->timezone_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand (d->timezone_label, TRUE);
    d->tz_btn = gtk_button_new_with_label (_("Change\xe2\x80\xa6"));

    GtkWidget *tz_row = make_labelled_row (_("Timezone:"), d->timezone_label, d->tz_btn);
    gtk_widget_set_margin_bottom (tz_row, 8);
    gtk_box_pack_start (GTK_BOX (page), tz_row, FALSE, FALSE, 0);

    d->ntp_sw = gtk_switch_new ();
    gtk_widget_set_halign (d->ntp_sw, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (page),
                        make_labelled_row (_("Automatic (NTP):"), d->ntp_sw, NULL),
                        FALSE, FALSE, 0);

    g_signal_connect (d->ntp_sw, "state-set", G_CALLBACK (on_ntp_toggled), d);
    g_signal_connect (d->tz_btn, "clicked",   G_CALLBACK (on_tz_clicked),  d);

    update_clock (d);
    d->clock_tick_id = g_timeout_add_seconds (1, update_clock, d);

    GError *err = NULL;
    d->td_proxy = g_dbus_proxy_new_for_bus_sync (
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
        TIMEDATE_BUS, TIMEDATE_PATH, TIMEDATE_IFACE, NULL, &err);

    if (err) {
        g_warning ("cc-datetime: timedated unavailable: %s", err->message);
        g_clear_error (&err);
        gtk_widget_set_sensitive (d->ntp_sw, FALSE);
        gtk_widget_set_sensitive (d->tz_btn, FALSE);
    } else {
        g_signal_connect (d->td_proxy, "g-properties-changed",
                          G_CALLBACK (on_td_properties_changed), d);
        load_timedate_info (d);
    }

    gtk_widget_show_all (scroll);
    return scroll;
}
