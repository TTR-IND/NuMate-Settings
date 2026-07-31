/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-sound.c — Sound settings panel
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
 * PulseAudio / ALSA backend provided by libmatemixer:
 *   Copyright (C) 2014 Michal Ratajsky <michal.ratajsky@gmail.com>
 *   Copyright (C) 2014-2021 MATE Developers
 *   Licensed under the GNU Lesser General Public License v2 or later.
 *   https://github.com/mate-desktop/libmatemixer
 * -----------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for J. A. Wheatstone, Torfaen Technology
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - Stream-then-control resolution is centralised in active_control();
 *     each handler previously repeated it with its own NULL checks.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-sound"

#include <string.h>
#include <libmatemixer/matemixer.h>

#include "cc-common.h"
#include "cc-panels.h"

#define SOUND_SCHEMA "org.gnome.desktop.sound"
#define KEY_EVENT    "event-sounds"
#define KEY_FEEDBACK "input-feedback-sounds"

typedef struct {
    MateMixerContext *ctx;
    GSettings        *sound_settings;

    GtkWidget *out_device_combo;
    GtkWidget *out_port_combo;
    GtkWidget *out_vol_scale;
    GtkWidget *out_mute_btn;
    GtkWidget *out_balance_scale;

    GtkWidget *in_device_combo;
    GtkWidget *in_vol_scale;
    GtkWidget *in_mute_btn;
    GtkWidget *in_level_bar;

    GtkWidget *event_sounds_sw;
    GtkWidget *input_feedback_sw;

    MateMixerStreamControl *in_monitor_ctrl;

    /* Set while the UI is being written from backend state, so that the
     * widget handlers do not echo the value straight back to the mixer. */
    gboolean   updating;
} SoundData;

/* ------------------------------------------------------------------ */
/*  Volume conversion                                                   */
/* ------------------------------------------------------------------ */

static gdouble
vol_to_pct (MateMixerStreamControl *ctrl)
{
    guint vol  = mate_mixer_stream_control_get_volume (ctrl);
    guint norm = mate_mixer_stream_control_get_normal_volume (ctrl);

    if (norm == 0) return 0.0;
    return CLAMP ((gdouble) vol / norm * 100.0, 0.0, 100.0);
}

static guint
pct_to_vol (MateMixerStreamControl *ctrl, gdouble pct)
{
    guint norm = mate_mixer_stream_control_get_normal_volume (ctrl);
    return (guint) (pct / 100.0 * norm + 0.5);
}

/*
 * Single resolver for "the control the user is currently driving".
 * Every handler funnels through it instead of repeating the
 * stream-then-control dance with its own NULL checks.
 */
static MateMixerStreamControl *
active_control (SoundData *d, MateMixerDirection dir)
{
    MateMixerStream *stream = (dir == MATE_MIXER_DIRECTION_OUTPUT)
        ? mate_mixer_context_get_default_output_stream (d->ctx)
        : mate_mixer_context_get_default_input_stream (d->ctx);

    return stream ? mate_mixer_stream_get_default_control (stream) : NULL;
}

/* ------------------------------------------------------------------ */
/*  Port switch helpers                                                 */
/* ------------------------------------------------------------------ */

static MateMixerSwitch *
find_port_switch (MateMixerStream *stream)
{
    for (const GList *l = mate_mixer_stream_list_switches (stream); l; l = l->next) {
        MateMixerStreamSwitch *sw = MATE_MIXER_STREAM_SWITCH (l->data);

        if (mate_mixer_stream_switch_get_role (sw) ==
            MATE_MIXER_STREAM_SWITCH_ROLE_PORT)
            return MATE_MIXER_SWITCH (sw);
    }
    return NULL;
}

/* Port name is stored as the combo id, so selection is a string compare
 * against the backend name rather than an index into a parallel array. */
static void
populate_port_combo (GtkWidget *combo, MateMixerSwitch *sw)
{
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (combo));
    if (!sw) return;

    for (const GList *l = mate_mixer_switch_list_options (sw); l; l = l->next) {
        MateMixerSwitchOption *opt = MATE_MIXER_SWITCH_OPTION (l->data);
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo),
                                   mate_mixer_switch_option_get_name (opt),
                                   mate_mixer_switch_option_get_label (opt));
    }

    MateMixerSwitchOption *active = mate_mixer_switch_get_active_option (sw);
    if (active)
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo),
                                     mate_mixer_switch_option_get_name (active));
}

/* ------------------------------------------------------------------ */
/*  Refresh from current default streams                                */
/* ------------------------------------------------------------------ */

static void
set_mute_button (GtkWidget *button, gboolean muted)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), muted);
    gtk_button_set_label (GTK_BUTTON (button), muted ? _("Unmute") : _("Mute"));
}

static void
refresh_output (SoundData *d)
{
    MateMixerStream *stream = mate_mixer_context_get_default_output_stream (d->ctx);
    if (!stream) return;

    MateMixerStreamControl *ctrl = mate_mixer_stream_get_default_control (stream);

    d->updating = TRUE;

    if (ctrl) {
        gtk_range_set_value (GTK_RANGE (d->out_vol_scale), vol_to_pct (ctrl));
        set_mute_button (d->out_mute_btn, mate_mixer_stream_control_get_mute (ctrl));

        gfloat balance = mate_mixer_stream_control_get_balance (ctrl);
        gtk_range_set_value (GTK_RANGE (d->out_balance_scale),
                             (gdouble) (balance + 1.0f) / 2.0 * 100.0);
    }

    gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->out_device_combo),
                                 mate_mixer_stream_get_name (stream));
    populate_port_combo (d->out_port_combo, find_port_switch (stream));

    d->updating = FALSE;
}

static void
refresh_input (SoundData *d)
{
    MateMixerStream *stream = mate_mixer_context_get_default_input_stream (d->ctx);
    if (!stream) return;

    MateMixerStreamControl *ctrl = mate_mixer_stream_get_default_control (stream);

    d->updating = TRUE;

    if (ctrl) {
        gtk_range_set_value (GTK_RANGE (d->in_vol_scale), vol_to_pct (ctrl));
        set_mute_button (d->in_mute_btn, mate_mixer_stream_control_get_mute (ctrl));
    }

    gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->in_device_combo),
                                 mate_mixer_stream_get_name (stream));

    d->updating = FALSE;
}

static void
populate_device_combos (SoundData *d)
{
    d->updating = TRUE;

    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (d->out_device_combo));
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (d->in_device_combo));

    for (const GList *l = mate_mixer_context_list_streams (d->ctx); l; l = l->next) {
        MateMixerStream *s = MATE_MIXER_STREAM (l->data);
        GtkWidget *combo;

        switch (mate_mixer_stream_get_direction (s)) {
        case MATE_MIXER_DIRECTION_OUTPUT: combo = d->out_device_combo; break;
        case MATE_MIXER_DIRECTION_INPUT:  combo = d->in_device_combo;  break;
        default:                          continue;
        }

        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo),
                                   mate_mixer_stream_get_name (s),
                                   mate_mixer_stream_get_label (s));
    }

    d->updating = FALSE;

    refresh_output (d);
    refresh_input  (d);
}

/* ------------------------------------------------------------------ */
/*  Input peak monitor                                                  */
/* ------------------------------------------------------------------ */

static void
on_monitor_value (MateMixerStreamControl *ctrl G_GNUC_UNUSED,
                  gdouble value, SoundData *d)
{
    gtk_level_bar_set_value (GTK_LEVEL_BAR (d->in_level_bar), CLAMP (value, 0.0, 1.0));
}

static void
stop_monitor (SoundData *d)
{
    if (!d->in_monitor_ctrl) return;

    mate_mixer_stream_control_set_monitor_enabled (d->in_monitor_ctrl, FALSE);
    g_signal_handlers_disconnect_by_func (d->in_monitor_ctrl, on_monitor_value, d);
    d->in_monitor_ctrl = NULL;
}

static void
start_monitor (SoundData *d)
{
    stop_monitor (d);

    MateMixerStreamControl *ctrl = active_control (d, MATE_MIXER_DIRECTION_INPUT);
    if (!ctrl) return;

    d->in_monitor_ctrl = ctrl;
    g_signal_connect (ctrl, "monitor-value", G_CALLBACK (on_monitor_value), d);
    mate_mixer_stream_control_set_monitor_enabled (ctrl, TRUE);
}

/* ------------------------------------------------------------------ */
/*  Context callbacks                                                   */
/* ------------------------------------------------------------------ */

static void
on_context_state (MateMixerContext *ctx G_GNUC_UNUSED,
                  GParamSpec *pspec G_GNUC_UNUSED, SoundData *d)
{
    if (mate_mixer_context_get_state (d->ctx) != MATE_MIXER_STATE_READY) return;

    populate_device_combos (d);
    start_monitor (d);
}

static void
on_stream_list_changed (MateMixerContext *ctx G_GNUC_UNUSED,
                        const gchar *name G_GNUC_UNUSED, SoundData *d)
{
    populate_device_combos (d);
}

static void
on_default_output_changed (MateMixerContext *ctx G_GNUC_UNUSED,
                           GParamSpec *pspec G_GNUC_UNUSED, SoundData *d)
{
    refresh_output (d);
}

static void
on_default_input_changed (MateMixerContext *ctx G_GNUC_UNUSED,
                          GParamSpec *pspec G_GNUC_UNUSED, SoundData *d)
{
    refresh_input (d);
    start_monitor (d);
}

/* ------------------------------------------------------------------ */
/*  Widget handlers                                                     */
/* ------------------------------------------------------------------ */

static void
out_vol_changed (GtkRange *range, SoundData *d)
{
    if (d->updating) return;

    MateMixerStreamControl *ctrl = active_control (d, MATE_MIXER_DIRECTION_OUTPUT);
    if (!ctrl) return;

    mate_mixer_stream_control_set_volume (ctrl,
        pct_to_vol (ctrl, gtk_range_get_value (range)));
}

static void
out_mute_toggled (GtkToggleButton *btn, SoundData *d)
{
    if (d->updating) return;

    MateMixerStreamControl *ctrl = active_control (d, MATE_MIXER_DIRECTION_OUTPUT);
    if (!ctrl) return;

    gboolean muted = gtk_toggle_button_get_active (btn);
    mate_mixer_stream_control_set_mute (ctrl, muted);
    gtk_button_set_label (GTK_BUTTON (btn), muted ? _("Unmute") : _("Mute"));
}

static void
out_balance_changed (GtkRange *range, SoundData *d)
{
    if (d->updating) return;

    MateMixerStreamControl *ctrl = active_control (d, MATE_MIXER_DIRECTION_OUTPUT);
    if (!ctrl) return;

    /* Slider runs 0..100; libmatemixer balance runs -1..+1. */
    mate_mixer_stream_control_set_balance (ctrl,
        (gfloat) (gtk_range_get_value (range) / 100.0 * 2.0 - 1.0));
}

static MateMixerStream *
find_stream_by_name (SoundData *d, const gchar *name)
{
    for (const GList *l = mate_mixer_context_list_streams (d->ctx); l; l = l->next) {
        MateMixerStream *s = MATE_MIXER_STREAM (l->data);
        if (g_strcmp0 (mate_mixer_stream_get_name (s), name) == 0)
            return s;
    }
    return NULL;
}

static void
out_device_changed (GtkComboBox *cb, SoundData *d)
{
    if (d->updating) return;

    const gchar *name = gtk_combo_box_get_active_id (cb);
    MateMixerStream *s = name ? find_stream_by_name (d, name) : NULL;
    if (s) mate_mixer_context_set_default_output_stream (d->ctx, s);
}

static void
in_device_changed (GtkComboBox *cb, SoundData *d)
{
    if (d->updating) return;

    const gchar *name = gtk_combo_box_get_active_id (cb);
    MateMixerStream *s = name ? find_stream_by_name (d, name) : NULL;
    if (s) mate_mixer_context_set_default_input_stream (d->ctx, s);
}

static void
out_port_changed (GtkComboBox *cb, SoundData *d)
{
    if (d->updating) return;

    const gchar *opt_name = gtk_combo_box_get_active_id (cb);
    if (!opt_name) return;

    MateMixerStream *s = mate_mixer_context_get_default_output_stream (d->ctx);
    if (!s) return;

    MateMixerSwitch *sw = find_port_switch (s);
    if (!sw) return;

    for (const GList *l = mate_mixer_switch_list_options (sw); l; l = l->next) {
        MateMixerSwitchOption *opt = MATE_MIXER_SWITCH_OPTION (l->data);
        if (g_strcmp0 (mate_mixer_switch_option_get_name (opt), opt_name) == 0) {
            mate_mixer_switch_set_active_option (sw, opt);
            return;
        }
    }
}

static void
in_vol_changed (GtkRange *range, SoundData *d)
{
    if (d->updating) return;

    MateMixerStreamControl *ctrl = active_control (d, MATE_MIXER_DIRECTION_INPUT);
    if (!ctrl) return;

    mate_mixer_stream_control_set_volume (ctrl,
        pct_to_vol (ctrl, gtk_range_get_value (range)));
}

static void
in_mute_toggled (GtkToggleButton *btn, SoundData *d)
{
    if (d->updating) return;

    MateMixerStreamControl *ctrl = active_control (d, MATE_MIXER_DIRECTION_INPUT);
    if (!ctrl) return;

    gboolean muted = gtk_toggle_button_get_active (btn);
    mate_mixer_stream_control_set_mute (ctrl, muted);
    gtk_button_set_label (GTK_BUTTON (btn), muted ? _("Unmute") : _("Mute"));
}

static gboolean
event_sounds_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, SoundData *d)
{
    g_settings_set_boolean (d->sound_settings, KEY_EVENT, state);
    return FALSE;
}

static gboolean
input_feedback_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, SoundData *d)
{
    g_settings_set_boolean (d->sound_settings, KEY_FEEDBACK, state);
    return FALSE;
}

static gchar *
format_pct (GtkScale *s G_GNUC_UNUSED, gdouble v, gpointer u G_GNUC_UNUSED)
{
    return g_strdup_printf ("%.0f%%", v);
}

/* ------------------------------------------------------------------ */
/*  Teardown                                                            */
/* ------------------------------------------------------------------ */

static void
sound_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, SoundData *d)
{
    stop_monitor (d);
    g_clear_object (&d->ctx);
    g_clear_object (&d->sound_settings);
    g_free (d);
}

/* ------------------------------------------------------------------ */
/*  Public                                                              */
/* ------------------------------------------------------------------ */

GtkWidget *
cc_sound_panel_new (void)
{
    SoundData *d = g_new0 (SoundData, 1);

    if (cc_schema_exists (SOUND_SCHEMA))
        d->sound_settings = g_settings_new (SOUND_SCHEMA);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (sound_panel_destroyed), d);

    /* ---- Output -------------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("OUTPUT")), FALSE, FALSE, 0);
    GtkGrid *og = GTK_GRID (cc_page_add_grid (page));

    d->out_device_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (og, 0, _("Device"), d->out_device_combo);

    d->out_port_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (og, 1, _("Port"), d->out_port_combo);

    d->out_vol_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_value_pos (GTK_SCALE (d->out_vol_scale), GTK_POS_RIGHT);
    g_signal_connect (d->out_vol_scale, "format-value", G_CALLBACK (format_pct), NULL);
    cc_grid_attach_row (og, 2, _("Volume"), d->out_vol_scale);

    d->out_mute_btn = gtk_toggle_button_new_with_label (_("Mute"));
    gtk_widget_set_halign (d->out_mute_btn, GTK_ALIGN_START);
    cc_grid_attach_row (og, 3, "", d->out_mute_btn);

    d->out_balance_scale = cc_marked_scale (0, 100, 1, "L", "R");
    gtk_scale_add_mark (GTK_SCALE (d->out_balance_scale), 50, GTK_POS_BOTTOM, "\xc2\xb7");
    cc_grid_attach_row (og, 4, _("Balance"), d->out_balance_scale);

    /* ---- Input --------------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("INPUT")), FALSE, FALSE, 0);
    GtkGrid *ig = GTK_GRID (cc_page_add_grid (page));

    d->in_device_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (ig, 0, _("Device"), d->in_device_combo);

    d->in_vol_scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_value_pos (GTK_SCALE (d->in_vol_scale), GTK_POS_RIGHT);
    g_signal_connect (d->in_vol_scale, "format-value", G_CALLBACK (format_pct), NULL);
    cc_grid_attach_row (ig, 1, _("Volume"), d->in_vol_scale);

    d->in_mute_btn = gtk_toggle_button_new_with_label (_("Mute"));
    gtk_widget_set_halign (d->in_mute_btn, GTK_ALIGN_START);
    cc_grid_attach_row (ig, 2, "", d->in_mute_btn);

    d->in_level_bar = gtk_level_bar_new_for_interval (0.0, 1.0);
    gtk_level_bar_set_mode (GTK_LEVEL_BAR (d->in_level_bar), GTK_LEVEL_BAR_MODE_CONTINUOUS);
    cc_grid_attach_row (ig, 3, _("Level"), d->in_level_bar);

    /* ---- Effects ------------------------------------------------- */
    if (d->sound_settings) {
        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("SOUND EFFECTS")),
                            FALSE, FALSE, 0);
        GtkGrid *fg = GTK_GRID (cc_page_add_grid (page));

        d->event_sounds_sw   = gtk_switch_new ();
        d->input_feedback_sw = gtk_switch_new ();
        gtk_widget_set_halign (d->event_sounds_sw,   GTK_ALIGN_START);
        gtk_widget_set_halign (d->input_feedback_sw, GTK_ALIGN_START);

        gtk_switch_set_active (GTK_SWITCH (d->event_sounds_sw),
            g_settings_get_boolean (d->sound_settings, KEY_EVENT));
        gtk_switch_set_active (GTK_SWITCH (d->input_feedback_sw),
            g_settings_get_boolean (d->sound_settings, KEY_FEEDBACK));

        cc_grid_attach_row (fg, 0, _("Event sounds"),   d->event_sounds_sw);
        cc_grid_attach_row (fg, 1, _("Input feedback"), d->input_feedback_sw);

        g_signal_connect (d->event_sounds_sw,   "state-set",
                          G_CALLBACK (event_sounds_toggled), d);
        g_signal_connect (d->input_feedback_sw, "state-set",
                          G_CALLBACK (input_feedback_toggled), d);
    }

    g_signal_connect (d->out_device_combo,  "changed",       G_CALLBACK (out_device_changed),  d);
    g_signal_connect (d->out_port_combo,    "changed",       G_CALLBACK (out_port_changed),    d);
    g_signal_connect (d->out_vol_scale,     "value-changed", G_CALLBACK (out_vol_changed),     d);
    g_signal_connect (d->out_mute_btn,      "toggled",       G_CALLBACK (out_mute_toggled),    d);
    g_signal_connect (d->out_balance_scale, "value-changed", G_CALLBACK (out_balance_changed), d);
    g_signal_connect (d->in_device_combo,   "changed",       G_CALLBACK (in_device_changed),   d);
    g_signal_connect (d->in_vol_scale,      "value-changed", G_CALLBACK (in_vol_changed),      d);
    g_signal_connect (d->in_mute_btn,       "toggled",       G_CALLBACK (in_mute_toggled),     d);

    mate_mixer_init ();

    d->ctx = mate_mixer_context_new ();
    mate_mixer_context_set_app_name (d->ctx, "NuMate Settings");
    mate_mixer_context_set_app_id   (d->ctx, "org.numate.settings");

    g_signal_connect (d->ctx, "notify::state",
                      G_CALLBACK (on_context_state), d);
    g_signal_connect (d->ctx, "stream-added",
                      G_CALLBACK (on_stream_list_changed), d);
    g_signal_connect (d->ctx, "stream-removed",
                      G_CALLBACK (on_stream_list_changed), d);
    g_signal_connect (d->ctx, "notify::default-output-stream",
                      G_CALLBACK (on_default_output_changed), d);
    g_signal_connect (d->ctx, "notify::default-input-stream",
                      G_CALLBACK (on_default_input_changed), d);

    mate_mixer_context_open (d->ctx);

    /* The context can already be READY when the daemon is warm; the
     * notify::state handler would then never fire. */
    if (mate_mixer_context_get_state (d->ctx) == MATE_MIXER_STATE_READY) {
        populate_device_combos (d);
        start_monitor (d);
    }

    gtk_widget_show_all (scroll);
    return scroll;
}
