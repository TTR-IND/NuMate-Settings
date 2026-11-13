/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-power.c — Power Management settings panel
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
 * GSettings schemas:
 *   org.mate.power-manager  — sleep-display-ac/battery,
 *                             sleep-computer-ac/battery,
 *                             button-lid-ac/battery, button-power
 *   org.gnome.settings-daemon.plugins.power — fallback button actions
 * Layout ported from the UKUI power plugin:
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
 *   - The anonymous typeof(*ctx) binding struct is now a named
 *     SleepBinding.
 *   - make_sleep_scale() builds, seeds and connects the combo in one
 *     place.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-power"

#include <stdlib.h>

#include "cc-common.h"
#include "cc-panels.h"

#define MATE_POWER_SCHEMA "org.mate.power-manager"

static const gchar *const POWER_SCHEMAS[] = {
    MATE_POWER_SCHEMA, "org.gnome.settings-daemon.plugins.power", NULL
};

/* org.mate.power-manager */
#define KEY_SLEEP_DISPLAY_AC    "sleep-display-ac"
#define KEY_SLEEP_DISPLAY_BATT  "sleep-display-battery"
#define KEY_SLEEP_COMPUTER_AC   "sleep-computer-ac"
#define KEY_SLEEP_COMPUTER_BATT "sleep-computer-battery"
#define KEY_LID_AC              "button-lid-ac"
#define KEY_BUTTON_POWER        "button-power"

/* gnome-settings-daemon fallback */
#define KEY_GNOME_LID       "lid-close-ac-action"
#define KEY_GNOME_POWER_BTN "power-button-action"

typedef struct {
    GSettings *power_s;
    gboolean   is_mate;   /* the sleep-timeout keys only exist on MATE */
} PowerData;

/* Timeouts are stored in seconds; the combo id is that integer verbatim,
 * so reading a selection back is one strtol with no lookup table. */
static const struct { const gchar *seconds; const gchar *label; } SLEEP_TIMES[] = {
    { "0",    N_("Never")      },
    { "60",   N_("1 minute")   },
    { "120",  N_("2 minutes")  },
    { "300",  N_("5 minutes")  },
    { "600",  N_("10 minutes") },
    { "900",  N_("15 minutes") },
    { "1800", N_("30 minutes") },
    { "3600", N_("1 hour")     },
};

static const struct { const gchar *id; const gchar *label; } BUTTON_ACTIONS[] = {
    { "nothing",   N_("Do nothing")   },
    { "suspend",   N_("Suspend")      },
    { "hibernate", N_("Hibernate")    },
    { "shutdown",  N_("Shut down")    },
    { "blank",     N_("Blank screen") },
    { "logout",    N_("Log out")      },
};

typedef struct { GSettings *settings; const gchar *key; } SleepBinding;

static gboolean
on_timer_toggled (GtkSwitch *sw, gboolean state, gpointer user_data G_GNUC_UNUSED)
{
    GtkWidget *scale = g_object_get_data (G_OBJECT (sw), "scale");
    GSettings *settings = g_object_get_data (G_OBJECT (sw), "settings");
    const gchar *key = g_object_get_data (G_OBJECT (sw), "key");

    if (scale) gtk_widget_set_sensitive (scale, state);
    if (settings && key) {
        if (!state) {
            g_settings_set_int (settings, key, 0); /* Never */
        } else {
            gint minutes = scale ? (gint) gtk_range_get_value (GTK_RANGE (scale)) : 10;
            if (minutes < 1) minutes = 10;
            g_settings_set_int (settings, key, minutes * 60);
        }
    }
    return FALSE;
}

static void
on_sleep_scale_changed (GtkRange *range, SleepBinding *binding)
{
    gint minutes = (gint) gtk_range_get_value (range);
    gint seconds = minutes * 60;
    CC_LOG ("%s -> %d s", binding->key, seconds);
    g_settings_set_int (binding->settings, binding->key, seconds);
}

static GtkWidget *
make_sleep_scale (GSettings *settings, const gchar *key)
{
    /* 0 = Never, then 1–60 minutes. */
    GtkWidget *scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 60, 1);
    gtk_scale_set_draw_value (GTK_SCALE (scale), TRUE);
    gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_RIGHT);
    gtk_widget_set_hexpand (scale, TRUE);
    gtk_widget_set_size_request (scale, 180, -1);

    if (!settings) {
        gtk_widget_set_sensitive (scale, FALSE);
        return scale;
    }

    gint seconds = g_settings_get_int (settings, key);
    gint minutes = seconds / 60;
    if (minutes < 0) minutes = 0;
    if (minutes > 60) minutes = 60;
    gtk_range_set_value (GTK_RANGE (scale), minutes);

    SleepBinding *binding = g_new0 (SleepBinding, 1);
    binding->settings = settings;
    binding->key      = key;
    g_signal_connect_data (scale, "value-changed", G_CALLBACK (on_sleep_scale_changed),
                           binding, cc_closure_free_data, 0);
    return scale;
}

static GtkWidget *
make_action_combo (GSettings *settings, const gchar *key)
{
    GtkWidget *combo = gtk_combo_box_text_new ();

    for (gsize i = 0; i < G_N_ELEMENTS (BUTTON_ACTIONS); i++)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo),
                                   BUTTON_ACTIONS[i].id, _(BUTTON_ACTIONS[i].label));

    if (!settings) {
        gtk_widget_set_sensitive (combo, FALSE);
        return combo;
    }

    /* active-id binds directly because the key is already the action string. */
    g_settings_bind (settings, key, combo, "active-id", G_SETTINGS_BIND_DEFAULT);
    return combo;
}

static void
power_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, PowerData *d)
{
    g_clear_object (&d->power_s);
    g_free (d);
}

GtkWidget *
cc_power_panel_new (void)
{
    PowerData *d = g_new0 (PowerData, 1);
    const gchar *schema = NULL;

    d->power_s = cc_settings_resolve (POWER_SCHEMAS, &schema);
    d->is_mate = (g_strcmp0 (schema, MATE_POWER_SCHEMA) == 0);

    if (!d->power_s)
        g_warning ("cc-power: no power-manager schema is installed");

    /* Sleep timeouts only exist on the MATE schema. */
    GSettings *sleep_s = d->is_mate ? d->power_s : NULL;

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (power_panel_destroyed), d);

    /* ---- On Mains ------------------------------------------------ */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ON MAINS")), FALSE, FALSE, 0);
    GtkWidget *ac_card = cc_section_card ();
    GtkWidget *ac_content = cc_section_card_content (ac_card);

    GtkWidget *ac_disp_sw = gtk_switch_new ();
    gtk_box_pack_start (GTK_BOX (ac_content),
        cc_row_new (_("Turn off display"), NULL, ac_disp_sw), FALSE, FALSE, 0);
    GtkWidget *ac_disp_scale = make_sleep_scale (sleep_s, KEY_SLEEP_DISPLAY_AC);
    gtk_widget_set_margin_start (ac_disp_scale, 24);
    gtk_widget_set_margin_end (ac_disp_scale, 12);
    gtk_widget_set_margin_bottom (ac_disp_scale, 8);
    gtk_box_pack_start (GTK_BOX (ac_content), ac_disp_scale, FALSE, FALSE, 0);
    {
        gint s = sleep_s ? g_settings_get_int (sleep_s, KEY_SLEEP_DISPLAY_AC) : 0;
        gtk_switch_set_active (GTK_SWITCH (ac_disp_sw), s > 0);
        gtk_widget_set_sensitive (ac_disp_scale, s > 0);
        g_object_set_data (G_OBJECT (ac_disp_sw), "scale", ac_disp_scale);
        g_object_set_data (G_OBJECT (ac_disp_sw), "settings", sleep_s);
        g_object_set_data (G_OBJECT (ac_disp_sw), "key", (gpointer) KEY_SLEEP_DISPLAY_AC);
        g_signal_connect (ac_disp_sw, "state-set", G_CALLBACK (on_timer_toggled), NULL);
    }

    GtkWidget *ac_susp_sw = gtk_switch_new ();
    gtk_box_pack_start (GTK_BOX (ac_content),
        cc_row_new (_("Suspend"), NULL, ac_susp_sw), FALSE, FALSE, 0);
    GtkWidget *ac_susp_scale = make_sleep_scale (sleep_s, KEY_SLEEP_COMPUTER_AC);
    gtk_widget_set_margin_start (ac_susp_scale, 24);
    gtk_widget_set_margin_end (ac_susp_scale, 12);
    gtk_widget_set_margin_bottom (ac_susp_scale, 8);
    gtk_box_pack_start (GTK_BOX (ac_content), ac_susp_scale, FALSE, FALSE, 0);
    {
        gint s = sleep_s ? g_settings_get_int (sleep_s, KEY_SLEEP_COMPUTER_AC) : 0;
        gtk_switch_set_active (GTK_SWITCH (ac_susp_sw), s > 0);
        gtk_widget_set_sensitive (ac_susp_scale, s > 0);
        g_object_set_data (G_OBJECT (ac_susp_sw), "scale", ac_susp_scale);
        g_object_set_data (G_OBJECT (ac_susp_sw), "settings", sleep_s);
        g_object_set_data (G_OBJECT (ac_susp_sw), "key", (gpointer) KEY_SLEEP_COMPUTER_AC);
        g_signal_connect (ac_susp_sw, "state-set", G_CALLBACK (on_timer_toggled), NULL);
    }
    gtk_box_pack_start (GTK_BOX (page), ac_card, FALSE, FALSE, 0);

    /* ---- On Battery ---------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ON BATTERY")), FALSE, FALSE, 0);
    GtkWidget *bat_card = cc_section_card ();
    GtkWidget *bat_content = cc_section_card_content (bat_card);

    GtkWidget *bat_disp_sw = gtk_switch_new ();
    gtk_box_pack_start (GTK_BOX (bat_content),
        cc_row_new (_("Turn off display"), NULL, bat_disp_sw), FALSE, FALSE, 0);
    GtkWidget *bat_disp_scale = make_sleep_scale (sleep_s, KEY_SLEEP_DISPLAY_BATT);
    gtk_widget_set_margin_start (bat_disp_scale, 24);
    gtk_widget_set_margin_end (bat_disp_scale, 12);
    gtk_widget_set_margin_bottom (bat_disp_scale, 8);
    gtk_box_pack_start (GTK_BOX (bat_content), bat_disp_scale, FALSE, FALSE, 0);
    {
        gint s = sleep_s ? g_settings_get_int (sleep_s, KEY_SLEEP_DISPLAY_BATT) : 0;
        gtk_switch_set_active (GTK_SWITCH (bat_disp_sw), s > 0);
        gtk_widget_set_sensitive (bat_disp_scale, s > 0);
        g_object_set_data (G_OBJECT (bat_disp_sw), "scale", bat_disp_scale);
        g_object_set_data (G_OBJECT (bat_disp_sw), "settings", sleep_s);
        g_object_set_data (G_OBJECT (bat_disp_sw), "key", (gpointer) KEY_SLEEP_DISPLAY_BATT);
        g_signal_connect (bat_disp_sw, "state-set", G_CALLBACK (on_timer_toggled), NULL);
    }

    GtkWidget *bat_susp_sw = gtk_switch_new ();
    gtk_box_pack_start (GTK_BOX (bat_content),
        cc_row_new (_("Suspend"), NULL, bat_susp_sw), FALSE, FALSE, 0);
    GtkWidget *bat_susp_scale = make_sleep_scale (sleep_s, KEY_SLEEP_COMPUTER_BATT);
    gtk_widget_set_margin_start (bat_susp_scale, 24);
    gtk_widget_set_margin_end (bat_susp_scale, 12);
    gtk_widget_set_margin_bottom (bat_susp_scale, 8);
    gtk_box_pack_start (GTK_BOX (bat_content), bat_susp_scale, FALSE, FALSE, 0);
    {
        gint s = sleep_s ? g_settings_get_int (sleep_s, KEY_SLEEP_COMPUTER_BATT) : 0;
        gtk_switch_set_active (GTK_SWITCH (bat_susp_sw), s > 0);
        gtk_widget_set_sensitive (bat_susp_scale, s > 0);
        g_object_set_data (G_OBJECT (bat_susp_sw), "scale", bat_susp_scale);
        g_object_set_data (G_OBJECT (bat_susp_sw), "settings", sleep_s);
        g_object_set_data (G_OBJECT (bat_susp_sw), "key", (gpointer) KEY_SLEEP_COMPUTER_BATT);
        g_signal_connect (bat_susp_sw, "state-set", G_CALLBACK (on_timer_toggled), NULL);
    }
    gtk_box_pack_start (GTK_BOX (page), bat_card, FALSE, FALSE, 0);

    GtkWidget *btn_card = cc_section_card ();
    GtkWidget *btn_content = cc_section_card_content (btn_card);
    gtk_box_pack_start (GTK_BOX (btn_content),
        cc_row_new (_("Power button"), NULL,
                    make_action_combo (d->power_s,
                        d->is_mate ? KEY_BUTTON_POWER : KEY_GNOME_POWER_BTN)),
        FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (btn_content),
        cc_row_new (_("Lid closed"), NULL,
                    make_action_combo (d->power_s,
                        d->is_mate ? KEY_LID_AC : KEY_GNOME_LID)),
        FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), btn_card, FALSE, FALSE, 0);

    gtk_widget_show_all (scroll);
    return scroll;
}
