/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-users.c — User Accounts settings panel
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
 * Backend: org.freedesktop.Accounts DBus service (AccountsService)
 * Layout ported from the UKUI userinfo plugin:
 *   Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/ukui/ukui-control-center
 * -----------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-users"

#include <unistd.h>

#include "cc-common.h"
#include "cc-panels.h"

#define ACCOUNTS_BUS   "org.freedesktop.Accounts"
#define ACCOUNTS_PATH  "/org/freedesktop/Accounts"
#define ACCOUNTS_IFACE "org.freedesktop.Accounts"
#define USER_IFACE     "org.freedesktop.Accounts.User"

#define ACCOUNT_TYPE_ADMIN 1
#define FACE_PIXELS       80

typedef struct {
    GDBusProxy *accounts_proxy;
    GDBusProxy *user_proxy;

    GtkWidget *face_image;
    GtkWidget *name_label;
    GtkWidget *type_label;
    GtkWidget *username_label;

    GtkWidget *real_name_entry;
    GtkWidget *change_name_btn;
    GtkWidget *change_pwd_btn;
} UsersData;

/* ------------------------------------------------------------------ */
/*  AccountsService                                                     */
/* ------------------------------------------------------------------ */

static gchar *
cached_string (GDBusProxy *proxy, const gchar *property)
{
    GVariant *v = g_dbus_proxy_get_cached_property (proxy, property);
    if (!v) return NULL;

    gchar *s = g_variant_dup_string (v, NULL);
    g_variant_unref (v);
    return s;
}

static void
load_user_info (UsersData *d)
{
    if (!d->user_proxy) return;

    gchar *real_name = cached_string (d->user_proxy, "RealName");
    if (real_name) {
        gtk_label_set_text (GTK_LABEL (d->name_label), real_name);
        gtk_entry_set_text (GTK_ENTRY (d->real_name_entry), real_name);
        g_free (real_name);
    }

    gchar *username = cached_string (d->user_proxy, "UserName");
    if (username) {
        gchar *at = g_strconcat ("@", username, NULL);
        gtk_label_set_text (GTK_LABEL (d->username_label), at);
        g_free (at);
        g_free (username);
    }

    GVariant *v = g_dbus_proxy_get_cached_property (d->user_proxy, "AccountType");
    if (v) {
        gint32 type = g_variant_get_int32 (v);
        gtk_label_set_text (GTK_LABEL (d->type_label),
                            type == ACCOUNT_TYPE_ADMIN ? _("Administrator")
                                                       : _("Standard user"));
        g_variant_unref (v);
    }

    gchar *icon_path = cached_string (d->user_proxy, "IconFile");
    if (icon_path && g_file_test (icon_path, G_FILE_TEST_EXISTS)) {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale (
            icon_path, FACE_PIXELS, FACE_PIXELS, TRUE, NULL);
        if (pb) {
            gtk_image_set_from_pixbuf (GTK_IMAGE (d->face_image), pb);
            g_object_unref (pb);
        }
    }
    g_free (icon_path);
}

static void
on_user_properties_changed (GDBusProxy *proxy G_GNUC_UNUSED,
                            GVariant *changed G_GNUC_UNUSED,
                            GStrv invalidated G_GNUC_UNUSED,
                            UsersData *d)
{
    load_user_info (d);
}

/* ------------------------------------------------------------------ */
/*  Handlers                                                            */
/* ------------------------------------------------------------------ */

static void
on_change_name_clicked (GtkButton *btn G_GNUC_UNUSED, UsersData *d)
{
    if (!d->user_proxy) return;

    const gchar *new_name = gtk_entry_get_text (GTK_ENTRY (d->real_name_entry));
    g_dbus_proxy_call (d->user_proxy, "SetRealName",
                       g_variant_new ("(s)", new_name),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void
on_change_pwd_clicked (GtkButton *btn G_GNUC_UNUSED, UsersData *d G_GNUC_UNUSED)
{
    static const gchar *const terminals[] = {
        "mate-terminal", "xterm", "x-terminal-emulator"
    };

    /* passwd owns the interactive dialogue; a terminal is the smallest
     * correct front end for it. */
    for (gsize i = 0; i < G_N_ELEMENTS (terminals); i++) {
        gchar *cmd = g_strdup_printf ("%s -e passwd", terminals[i]);
        gboolean launched = g_spawn_command_line_async (cmd, NULL);
        g_free (cmd);

        if (launched) return;
    }

    g_warning ("cc-users: no terminal emulator available to run passwd");
}

static void
users_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, UsersData *d)
{
    g_clear_object (&d->user_proxy);
    g_clear_object (&d->accounts_proxy);
    g_free (d);
}

/* ------------------------------------------------------------------ */
/*  Connection                                                          */
/* ------------------------------------------------------------------ */

static GDBusProxy *
find_current_user_proxy (GDBusProxy *accounts, GError **error)
{
    GVariant *ret = g_dbus_proxy_call_sync (
        accounts, "FindUserById",
        g_variant_new ("(x)", (gint64) getuid ()),
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);

    if (!ret) return NULL;

    const gchar *user_path = NULL;
    g_variant_get (ret, "(&o)", &user_path);
    CC_LOG ("current user object: %s", user_path);

    GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync (
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
        ACCOUNTS_BUS, user_path, USER_IFACE, NULL, error);

    g_variant_unref (ret);
    return proxy;
}

/* ------------------------------------------------------------------ */
/*  Public                                                              */
/* ------------------------------------------------------------------ */

GtkWidget *
cc_users_panel_new (void)
{
    UsersData *d = g_new0 (UsersData, 1);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (users_panel_destroyed), d);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("CURRENT USER")),
                        FALSE, FALSE, 0);

    GtkWidget *card = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_bottom (card, 16);
    gtk_box_pack_start (GTK_BOX (page), card, FALSE, FALSE, 0);

    d->face_image = gtk_image_new_from_icon_name ("avatar-default", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (d->face_image), FACE_PIXELS);
    gtk_widget_set_valign (d->face_image, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (card), d->face_image, FALSE, FALSE, 0);

    GtkWidget *info = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign (info, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (card), info, TRUE, TRUE, 0);

    d->name_label = gtk_label_new (_("Loading\xe2\x80\xa6"));
    gtk_widget_set_halign (d->name_label, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->name_label), "title-2");
    gtk_box_pack_start (GTK_BOX (info), d->name_label, FALSE, FALSE, 0);

    d->username_label = gtk_label_new ("");
    gtk_widget_set_halign (d->username_label, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->username_label), "dim-label");
    gtk_box_pack_start (GTK_BOX (info), d->username_label, FALSE, FALSE, 0);

    d->type_label = gtk_label_new ("");
    gtk_widget_set_halign (d->type_label, GTK_ALIGN_START);
    gtk_box_pack_start (GTK_BOX (info), d->type_label, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ACCOUNT")), FALSE, FALSE, 0);

    GtkWidget *name_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom (name_row, 8);
    GtkWidget *name_lbl = gtk_label_new (_("Full name:"));
    gtk_label_set_width_chars (GTK_LABEL (name_lbl), 12);
    gtk_widget_set_halign (name_lbl, GTK_ALIGN_END);

    d->real_name_entry = gtk_entry_new ();
    gtk_widget_set_hexpand (d->real_name_entry, TRUE);
    d->change_name_btn = gtk_button_new_with_label (_("Apply"));

    gtk_box_pack_start (GTK_BOX (name_row), name_lbl,           FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (name_row), d->real_name_entry, TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (name_row), d->change_name_btn, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), name_row, FALSE, FALSE, 0);

    d->change_pwd_btn = gtk_button_new_with_label (_("Change password\xe2\x80\xa6"));
    gtk_widget_set_halign (d->change_pwd_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_top (d->change_pwd_btn, 8);
    gtk_box_pack_start (GTK_BOX (page), d->change_pwd_btn, FALSE, FALSE, 0);

    g_signal_connect (d->change_name_btn, "clicked",
                      G_CALLBACK (on_change_name_clicked), d);
    g_signal_connect (d->change_pwd_btn, "clicked",
                      G_CALLBACK (on_change_pwd_clicked), d);

    GError *err = NULL;
    d->accounts_proxy = g_dbus_proxy_new_for_bus_sync (
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
        ACCOUNTS_BUS, ACCOUNTS_PATH, ACCOUNTS_IFACE, NULL, &err);

    if (!d->accounts_proxy) {
        g_warning ("cc-users: AccountsService unavailable: %s", err->message);
        g_clear_error (&err);
    } else {
        d->user_proxy = find_current_user_proxy (d->accounts_proxy, &err);

        if (!d->user_proxy) {
            g_warning ("cc-users: cannot resolve current user: %s", err->message);
            g_clear_error (&err);
        } else {
            g_signal_connect (d->user_proxy, "g-properties-changed",
                              G_CALLBACK (on_user_properties_changed), d);
            load_user_info (d);
        }
    }

    if (!d->user_proxy) {
        gtk_label_set_text (GTK_LABEL (d->name_label), _("Account service unavailable"));
        gtk_widget_set_sensitive (d->real_name_entry, FALSE);
        gtk_widget_set_sensitive (d->change_name_btn, FALSE);
    }

    gtk_widget_show_all (scroll);
    return scroll;
}
