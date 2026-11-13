/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * numate-settings.c — application entry point
 *
 * NuMate Settings — a unified desktop settings application.
 *
 * Copyright (C) 2026 Josh Wheatstone TechnyxLabs
 * Copyright (C) 2025 NuMate desktop contributors
 * Copyright (C) 2025 ukui-control-center contributors
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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * -----------------------------------------------------------------------
 * Third-party components retain their own licences; see the headers of
 * the individual panel and backend files:
 *   libmatemixer          (C) 2014 Michal Ratajsky, 2014-2021 MATE Developers
 *   mate-control-center   (C) 2003-2006 Novell, (C) 2007-2010 GNOME Foundation
 *   libmate-desktop       (C) 2000-2001 Ximian, 2014-2021 MATE Developers
 *   mate-screensaver      (C) 2004-2006 William Jon McCann,
 *                             2012-2021 MATE Developers
 *   libXcursor            (C) 2002 Keith Packard
 *   NetworkManager/libnm  (C) 2004-2024 Red Hat, Inc.
 *   ukui-control-center   (C) 2019 Tianjin KYLIN Information Technology Co.
 * -----------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "numate-settings"

#include <locale.h>

#include "cc-common.h"
#include "cc-window.h"

#define APP_ID "org.numate.settings"

static const gchar *const CSS_PATHS[] = {
    "/usr/share/numate-settings/style.css",
    "data/style.css",   /* uninstalled, run from the build tree */
    NULL
};

/* Styling is optional: a missing sheet costs the theme accents, not
 * functionality, so it is a warning rather than a fatal error. */
static void
load_css (void)
{
    for (int i = 0; CSS_PATHS[i]; i++) {
        if (!g_file_test (CSS_PATHS[i], G_FILE_TEST_EXISTS)) continue;

        GtkCssProvider *provider = gtk_css_provider_new ();
        GError *err = NULL;

        if (gtk_css_provider_load_from_path (provider, CSS_PATHS[i], &err)) {
            gtk_style_context_add_provider_for_screen (
                gdk_screen_get_default (), GTK_STYLE_PROVIDER (provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            CC_LOG ("loaded stylesheet %s", CSS_PATHS[i]);
        } else {
            g_warning ("numate-settings: cannot parse %s: %s",
                       CSS_PATHS[i], err->message);
            g_clear_error (&err);
        }

        g_object_unref (provider);
        return;
    }

    CC_LOG ("no stylesheet found; using theme defaults");
}

/*
 * The application owns the window list, so there is no file-scope global
 * to keep in step with it. Both entry paths resolve the window the same
 * way: reuse the active one, or build the first.
 */
static CcWindow *
ensure_window (GtkApplication *app)
{
    GtkWindow *active = gtk_application_get_active_window (app);
    if (active && CC_IS_WINDOW (active))
        return CC_WINDOW (active);

    return CC_WINDOW (cc_window_new (app));
}

static void
on_activate (GtkApplication *app, gpointer user_data G_GNUC_UNUSED)
{
    gtk_window_present (GTK_WINDOW (ensure_window (app)));
}

static gint
on_command_line (GtkApplication *app, GApplicationCommandLine *cmdline,
                 gpointer user_data G_GNUC_UNUSED)
{
    GVariantDict *opts = g_application_command_line_get_options_dict (cmdline);
    CcWindow *window = ensure_window (app);

    const gchar *panel_id = NULL;
    if (g_variant_dict_lookup (opts, "panel", "&s", &panel_id))
        cc_window_show_panel (window, panel_id);

    gtk_window_present (GTK_WINDOW (window));
    return 0;
}

static const GOptionEntry OPTION_ENTRIES[] = {
    { "panel", 'p', 0, G_OPTION_ARG_STRING, NULL,
      "Open a specific settings panel by id", "PANEL" },
    { NULL, 0, 0, 0, NULL, NULL, NULL }
};

int
main (int argc, char **argv)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    GtkApplication *app = gtk_application_new (APP_ID,
                                               G_APPLICATION_HANDLES_COMMAND_LINE);

    g_application_add_main_option_entries (G_APPLICATION (app), OPTION_ENTRIES);
    g_application_set_option_context_summary (
        G_APPLICATION (app), "NuMate Settings \xe2\x80\x94 unified desktop configuration.");

    g_signal_connect (app, "startup",      G_CALLBACK (load_css),        NULL);
    g_signal_connect (app, "activate",     G_CALLBACK (on_activate),     NULL);
    g_signal_connect (app, "command-line", G_CALLBACK (on_command_line), NULL);

    int status = g_application_run (G_APPLICATION (app), argc, argv);

    g_object_unref (app);
    return status;
}
