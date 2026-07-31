/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-mouse.c — Mouse & Touchpad settings panel
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
 * GSettings schemas:
 *   org.gnome.desktop.peripherals.mouse    — speed, double-click,
 *                                            left-handed, natural-scroll,
 *                                            middle-click-emulation
 *   org.gnome.desktop.peripherals.touchpad — send-events, tap-to-click,
 *                                            natural-scroll,
 *                                            disable-while-typing,
 *                                            two-finger-scrolling-enabled,
 *                                            edge-scrolling-enabled
 * Layout ported from the UKUI mouse plugin:
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
 *   - The local add_bound_scale() helper, duplicated verbatim here and
 *     in cc-keyboard.c, is replaced by cc_grid_attach_bound_scale().
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-mouse"

#include "cc-common.h"
#include "cc-panels.h"

#define MOUSE_SCHEMA    "org.gnome.desktop.peripherals.mouse"
#define TOUCHPAD_SCHEMA "org.gnome.desktop.peripherals.touchpad"

#define KEY_LEFT_HANDED    "left-handed"
#define KEY_SPEED          "speed"
#define KEY_DOUBLE_CLICK   "double-click"
#define KEY_NATURAL_SCROLL "natural-scroll"
#define KEY_MIDDLE_CLICK   "middle-click-emulation"

#define KEY_TP_SEND_EVENTS          "send-events"
#define KEY_TP_TAP_CLICK            "tap-to-click"
#define KEY_TP_NATURAL_SCROLL       "natural-scroll"
#define KEY_TP_DISABLE_WHILE_TYPING "disable-while-typing"
#define KEY_TP_TWO_FINGER           "two-finger-scrolling-enabled"
#define KEY_TP_EDGE_SCROLL          "edge-scrolling-enabled"

typedef struct {
    GSettings *mouse_s;
    GSettings *touchpad_s;
} MouseData;

/* send-events is an enum string, so it cannot bind straight to "active". */
static gboolean
on_touchpad_enabled_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, GSettings *s)
{
    g_settings_set_string (s, KEY_TP_SEND_EVENTS, state ? "enabled" : "disabled");
    return FALSE;
}

static void
mouse_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, MouseData *d)
{
    g_clear_object (&d->mouse_s);
    g_clear_object (&d->touchpad_s);
    g_free (d);
}

GtkWidget *
cc_mouse_panel_new (void)
{
    MouseData *d = g_new0 (MouseData, 1);

    if (cc_schema_exists (MOUSE_SCHEMA))
        d->mouse_s = g_settings_new (MOUSE_SCHEMA);
    else
        g_warning ("cc-mouse: schema '%s' is not installed", MOUSE_SCHEMA);

    if (cc_schema_exists (TOUCHPAD_SCHEMA))
        d->touchpad_s = g_settings_new (TOUCHPAD_SCHEMA);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (mouse_panel_destroyed), d);

    if (d->mouse_s) {
        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("MOUSE")), FALSE, FALSE, 0);
        GtkGrid *mg = GTK_GRID (cc_page_add_grid (page));

        cc_grid_attach_row (mg, 0, _("Left-handed"),
                            cc_settings_switch (d->mouse_s, KEY_LEFT_HANDED));
        cc_grid_attach_row (mg, 1, _("Middle click emulation"),
                            cc_settings_switch (d->mouse_s, KEY_MIDDLE_CLICK));
        cc_grid_attach_row (mg, 2, _("Natural scrolling"),
                            cc_settings_switch (d->mouse_s, KEY_NATURAL_SCROLL));

        cc_grid_attach_bound_scale (mg, 3, _("Pointer speed"), d->mouse_s, KEY_SPEED,
                         -1.0, 1.0, 0.05, _("Slow"), _("Fast"));
        cc_grid_attach_bound_scale (mg, 4, _("Double-click speed"), d->mouse_s, KEY_DOUBLE_CLICK,
                         100, 1000, 50, _("Fast"), _("Slow"));
    }

    if (d->touchpad_s) {
        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("TOUCHPAD")), FALSE, FALSE, 0);
        GtkGrid *tg = GTK_GRID (cc_page_add_grid (page));

        GtkWidget *tp_enabled = gtk_switch_new ();
        gtk_widget_set_halign (tp_enabled, GTK_ALIGN_START);

        gchar *send_events = g_settings_get_string (d->touchpad_s, KEY_TP_SEND_EVENTS);
        gtk_switch_set_active (GTK_SWITCH (tp_enabled),
                               g_strcmp0 (send_events, "enabled") == 0);
        g_free (send_events);

        g_signal_connect (tp_enabled, "state-set",
                          G_CALLBACK (on_touchpad_enabled_toggled), d->touchpad_s);
        cc_grid_attach_row (tg, 0, _("Enable touchpad"), tp_enabled);

        cc_grid_attach_row (tg, 1, _("Tap to click"),
                            cc_settings_switch (d->touchpad_s, KEY_TP_TAP_CLICK));
        cc_grid_attach_row (tg, 2, _("Natural scrolling"),
                            cc_settings_switch (d->touchpad_s, KEY_TP_NATURAL_SCROLL));
        cc_grid_attach_row (tg, 3, _("Disable while typing"),
                            cc_settings_switch (d->touchpad_s, KEY_TP_DISABLE_WHILE_TYPING));
        cc_grid_attach_row (tg, 4, _("Two-finger scrolling"),
                            cc_settings_switch (d->touchpad_s, KEY_TP_TWO_FINGER));
        cc_grid_attach_row (tg, 5, _("Edge scrolling"),
                            cc_settings_switch (d->touchpad_s, KEY_TP_EDGE_SCROLL));
    }

    gtk_widget_show_all (scroll);
    return scroll;
}
