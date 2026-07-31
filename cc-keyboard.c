/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-keyboard.c — Keyboard settings panel
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
 * GSettings schemas used:
 *   org.gnome.desktop.peripherals.keyboard   — repeat, delay, rate
 *   org.gnome.desktop.a11y.keyboard          — sticky, slow, bounce, mouse keys
 *
 * Schema documentation from mate-control-center and GNOME Settings:
 *   Copyright (C) 2001-2012 MATE/GNOME Developers
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/mate-desktop/mate-control-center
 * -----------------------------------------------------------------------
 *
 * Layout ported from the UKUI keyboard plugin:
 *   Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/ukui/ukui-control-center
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for J. A. Wheatstone, Torfaen Technology
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - The local add_bound_scale() helper, duplicated verbatim here and
 *     in cc-mouse.c, is replaced by cc_grid_attach_bound_scale().
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-keyboard"

#include "cc-common.h"
#include "cc-panels.h"

#define KB_SCHEMA   "org.gnome.desktop.peripherals.keyboard"
#define A11Y_SCHEMA "org.gnome.desktop.a11y.keyboard"

#define KEY_REPEAT          "repeat"
#define KEY_DELAY           "delay"
#define KEY_REPEAT_INTERVAL "repeat-interval"

#define KEY_STICKY       "stickykeys-enable"
#define KEY_SLOW         "slowkeys-enable"
#define KEY_SLOW_DELAY   "slowkeys-delay"
#define KEY_BOUNCE       "bouncekeys-enable"
#define KEY_BOUNCE_DELAY "bouncekeys-delay"
#define KEY_MOUSEKEYS    "mousekeys-enable"
#define KEY_TOGGLEKEYS   "togglekeys-enable"

typedef struct {
    GSettings *kb_s;
    GSettings *a11y_s;

    /* Delay/rate rows: insensitive while key repeat is off. */
    GtkWidget *repeat_grid;
} KbData;

static gboolean
on_repeat_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, KbData *d)
{
    gtk_widget_set_sensitive (d->repeat_grid, state);
    return FALSE;
}

static void
kb_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, KbData *d)
{
    g_clear_object (&d->kb_s);
    g_clear_object (&d->a11y_s);
    g_free (d);
}

GtkWidget *
cc_keyboard_panel_new (void)
{
    KbData *d = g_new0 (KbData, 1);

    if (!cc_schema_exists (KB_SCHEMA)) {
        g_warning ("cc-keyboard: schema '%s' is not installed", KB_SCHEMA);
    } else {
        d->kb_s = g_settings_new (KB_SCHEMA);
    }

    if (cc_schema_exists (A11Y_SCHEMA))
        d->a11y_s = g_settings_new (A11Y_SCHEMA);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (kb_panel_destroyed), d);

    /* ---- Key repeat ---------------------------------------------- */
    if (d->kb_s) {
        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("KEY REPEAT")),
                            FALSE, FALSE, 0);
        GtkGrid *rg = GTK_GRID (cc_page_add_grid (page));

        GtkWidget *repeat_sw = cc_settings_switch (d->kb_s, KEY_REPEAT);
        cc_grid_attach_row (rg, 0, _("Key repeat"), repeat_sw);

        d->repeat_grid = cc_page_add_grid (page);
        gtk_widget_set_margin_start (d->repeat_grid, 12);

        cc_grid_attach_bound_scale (GTK_GRID (d->repeat_grid), 0, _("Repeat delay"),
                         d->kb_s, KEY_DELAY, 100, 2000, 10, _("Short"), _("Long"));
        cc_grid_attach_bound_scale (GTK_GRID (d->repeat_grid), 1, _("Repeat interval"),
                         d->kb_s, KEY_REPEAT_INTERVAL, 10, 100, 1, _("Short"), _("Long"));

        gtk_widget_set_sensitive (d->repeat_grid,
                                  g_settings_get_boolean (d->kb_s, KEY_REPEAT));
        g_signal_connect (repeat_sw, "state-set", G_CALLBACK (on_repeat_toggled), d);
    }

    /* ---- Accessibility ------------------------------------------- */
    if (d->a11y_s) {
        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ACCESSIBILITY")),
                            FALSE, FALSE, 0);
        GtkGrid *ag = GTK_GRID (cc_page_add_grid (page));
        gint row = 0;

        cc_grid_attach_row (ag, row++, _("Sticky keys"),
                            cc_settings_switch (d->a11y_s, KEY_STICKY));

        cc_grid_attach_row (ag, row++, _("Slow keys"),
                            cc_settings_switch (d->a11y_s, KEY_SLOW));
        cc_grid_attach_bound_scale (ag, row++, _("Slow keys delay"),
                         d->a11y_s, KEY_SLOW_DELAY, 0, 500, 10, _("Short"), _("Long"));

        cc_grid_attach_row (ag, row++, _("Bounce keys"),
                            cc_settings_switch (d->a11y_s, KEY_BOUNCE));
        cc_grid_attach_bound_scale (ag, row++, _("Bounce keys delay"),
                         d->a11y_s, KEY_BOUNCE_DELAY, 0, 500, 10, _("Short"), _("Long"));

        cc_grid_attach_row (ag, row++, _("Mouse keys (numpad)"),
                            cc_settings_switch (d->a11y_s, KEY_MOUSEKEYS));
        cc_grid_attach_row (ag, row++, _("Toggle keys (Caps/Num sound)"),
                            cc_settings_switch (d->a11y_s, KEY_TOGGLEKEYS));
    }

    gtk_widget_show_all (scroll);
    return scroll;
}
