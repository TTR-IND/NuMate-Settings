/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-power.c — Power Management settings panel
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
 * MODIFICATIONS (2026), made for J. A. Wheatstone, Torfaen Technology
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - The anonymous typeof(*ctx) binding struct is now a named
 *     SleepBinding.
 *   - make_sleep_combo() builds, seeds and connects the combo in one
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

static void
on_sleep_changed (GtkComboBox *cb, SleepBinding *binding)
{
    const gchar *id = gtk_combo_box_get_active_id (cb);
    if (!id) return;

    gint seconds = (gint) g_ascii_strtoll (id, NULL, 10);
    CC_LOG ("%s -> %d s", binding->key, seconds);
    g_settings_set_int (binding->settings, binding->key, seconds);
}

/*
 * Builds a sleep-timeout combo, seeds it from the key and connects the
 * writer. Four rows needed all three steps; doing them in one place keeps
 * the seed and the writer from drifting apart.
 */
static GtkWidget *
make_sleep_combo (GSettings *settings, const gchar *key)
{
    GtkWidget *combo = gtk_combo_box_text_new ();

    for (gsize i = 0; i < G_N_ELEMENTS (SLEEP_TIMES); i++)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo),
                                   SLEEP_TIMES[i].seconds, _(SLEEP_TIMES[i].label));

    if (!settings) {
        gtk_widget_set_sensitive (combo, FALSE);
        return combo;
    }

    gchar *current = g_strdup_printf ("%d", g_settings_get_int (settings, key));
    if (!gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), current))
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
    g_free (current);

    SleepBinding *binding = g_new0 (SleepBinding, 1);
    binding->settings = settings;
    binding->key      = key;

    g_signal_connect_data (combo, "changed", G_CALLBACK (on_sleep_changed),
                           binding, cc_closure_free_data, 0);
    return combo;
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

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ON AC POWER")),
                        FALSE, FALSE, 0);
    GtkGrid *acg = GTK_GRID (cc_page_add_grid (page));
    cc_grid_attach_row (acg, 0, _("Turn off display after"),
                        make_sleep_combo (sleep_s, KEY_SLEEP_DISPLAY_AC));
    cc_grid_attach_row (acg, 1, _("Suspend after"),
                        make_sleep_combo (sleep_s, KEY_SLEEP_COMPUTER_AC));

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ON BATTERY")),
                        FALSE, FALSE, 0);
    GtkGrid *batg = GTK_GRID (cc_page_add_grid (page));
    cc_grid_attach_row (batg, 0, _("Turn off display after"),
                        make_sleep_combo (sleep_s, KEY_SLEEP_DISPLAY_BATT));
    cc_grid_attach_row (batg, 1, _("Suspend after"),
                        make_sleep_combo (sleep_s, KEY_SLEEP_COMPUTER_BATT));

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("BUTTONS")),
                        FALSE, FALSE, 0);
    GtkGrid *btng = GTK_GRID (cc_page_add_grid (page));
    cc_grid_attach_row (btng, 0, _("When power button pressed"),
                        make_action_combo (d->power_s,
                            d->is_mate ? KEY_BUTTON_POWER : KEY_GNOME_POWER_BTN));
    cc_grid_attach_row (btng, 1, _("When lid is closed"),
                        make_action_combo (d->power_s,
                            d->is_mate ? KEY_LID_AC : KEY_GNOME_LID));

    gtk_widget_show_all (scroll);
    return scroll;
}
