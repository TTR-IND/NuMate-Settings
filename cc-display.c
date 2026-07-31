/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-display.c — Display settings panel
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
 * Backend: libXrandr — resolution, refresh rate, rotation, primary output.
 * Also reads/writes org.gnome.desktop.interface scaling-factor.
 * Layout ported from the UKUI display plugin:
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
 *   - Added mode_index (RRMode -> XRRModeInfo*), rebuilt per resource
 *     fetch. Four functions previously linear-scanned res->modes[]
 *     inside loops over an output's modes.
 *   - refresh_resources() is called after on_apply_clicked; the cached
 *     XRRScreenResources went stale once a mode was set.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-display"

#include <stdio.h>
#include <X11/extensions/Xrandr.h>
#include <gdk/gdkx.h>

#include "cc-common.h"
#include "cc-panels.h"

#define IFACE_SCHEMA "org.gnome.desktop.interface"
#define KEY_SCALING  "scaling-factor"

typedef struct {
    Display            *xdpy;
    Window              root;
    XRRScreenResources *res;

    /*
     * Mode id -> XRRModeInfo*. XRRScreenResources stores modes in a flat
     * array while outputs reference them by id, so every lookup used to be
     * a linear scan nested inside a loop over the output's modes. Building
     * the index once per resource fetch makes each lookup O(1) and the
     * whole population pass linear in the number of modes.
     */
    GHashTable         *mode_index;

    GSettings *iface_s;

    GtkWidget *monitor_combo;
    GtkWidget *res_combo;
    GtkWidget *rate_combo;
    GtkWidget *rotate_combo;
    GtkWidget *scale_combo;
    GtkWidget *primary_btn;
    GtkWidget *apply_btn;

    RROutput sel_output;
    RRMode   sel_mode;
    Rotation sel_rotation;

    gboolean updating;
} DisplayData;

/* ------------------------------------------------------------------ */
/*  XRandR access                                                       */
/* ------------------------------------------------------------------ */

static void
rebuild_mode_index (DisplayData *d)
{
    g_clear_pointer (&d->mode_index, g_hash_table_destroy);
    d->mode_index = g_hash_table_new (g_direct_hash, g_direct_equal);

    if (!d->res) return;

    for (int i = 0; i < d->res->nmode; i++)
        g_hash_table_insert (d->mode_index,
                             GUINT_TO_POINTER ((guint) d->res->modes[i].id),
                             &d->res->modes[i]);
}

static XRRModeInfo *
lookup_mode (DisplayData *d, RRMode id)
{
    return g_hash_table_lookup (d->mode_index, GUINT_TO_POINTER ((guint) id));
}

static gdouble
mode_refresh_hz (const XRRModeInfo *mi)
{
    if (!mi->hTotal || !mi->vTotal) return 0.0;
    return (gdouble) mi->dotClock / ((gdouble) mi->hTotal * (gdouble) mi->vTotal);
}

static RRMode
output_current_mode (DisplayData *d, const XRROutputInfo *oi)
{
    if (!oi->crtc) return None;

    XRRCrtcInfo *ci = XRRGetCrtcInfo (d->xdpy, d->res, oi->crtc);
    if (!ci) return None;

    RRMode mode = ci->mode;
    XRRFreeCrtcInfo (ci);
    return mode;
}

/* ------------------------------------------------------------------ */
/*  Combo population                                                    */
/* ------------------------------------------------------------------ */

static void
repopulate_rates (DisplayData *d, RRMode selected_mode)
{
    d->updating = TRUE;
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (d->rate_combo));

    XRROutputInfo *oi = XRRGetOutputInfo (d->xdpy, d->res, d->sel_output);
    XRRModeInfo *selected = lookup_mode (d, selected_mode);

    if (!oi || !selected) {
        if (oi) XRRFreeOutputInfo (oi);
        d->updating = FALSE;
        return;
    }

    gchar *active_id = NULL;

    for (int i = 0; i < oi->nmode; i++) {
        XRRModeInfo *mi = lookup_mode (d, oi->modes[i]);
        if (!mi) continue;
        if (mi->width != selected->width || mi->height != selected->height) continue;

        gdouble hz = mode_refresh_hz (mi);
        if (hz <= 0.0) continue;

        gchar *id  = g_strdup_printf ("%lu", (gulong) mi->id);
        gchar *lbl = g_strdup_printf ("%.2f Hz", hz);
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->rate_combo), id, lbl);

        if (mi->id == selected_mode) {
            g_free (active_id);
            active_id = g_strdup (id);
        }

        g_free (id);
        g_free (lbl);
    }

    XRRFreeOutputInfo (oi);

    if (active_id) {
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->rate_combo), active_id);
        g_free (active_id);
    } else {
        gtk_combo_box_set_active (GTK_COMBO_BOX (d->rate_combo), 0);
    }

    d->updating = FALSE;
}

static void
populate_resolutions (DisplayData *d)
{
    d->updating = TRUE;
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (d->res_combo));

    XRROutputInfo *oi = XRRGetOutputInfo (d->xdpy, d->res, d->sel_output);
    if (!oi) { d->updating = FALSE; return; }

    RRMode current = output_current_mode (d, oi);
    GHashTable *seen = g_hash_table_new (g_direct_hash, g_direct_equal);
    gchar *active_id = NULL;

    d->sel_mode = None;

    for (int i = 0; i < oi->nmode; i++) {
        XRRModeInfo *mi = lookup_mode (d, oi->modes[i]);
        if (!mi) continue;

        gpointer key = GUINT_TO_POINTER ((guint) mi->width << 16 | mi->height);
        if (g_hash_table_contains (seen, key)) continue;
        g_hash_table_add (seen, key);

        gchar *id  = g_strdup_printf ("%ux%u", mi->width, mi->height);
        gchar *lbl = g_strdup_printf ("%u \xc3\x97 %u", mi->width, mi->height);
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->res_combo), id, lbl);

        /* Prefer the mode the CRTC is actually driving; fall back to the
         * first advertised mode when the output is currently off. */
        if (mi->id == current || d->sel_mode == None) {
            g_free (active_id);
            active_id   = g_strdup (id);
            d->sel_mode = mi->id;
        }

        g_free (id);
        g_free (lbl);
    }

    g_hash_table_destroy (seen);
    XRRFreeOutputInfo (oi);

    if (active_id) {
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->res_combo), active_id);
        g_free (active_id);
    }

    d->updating = FALSE;

    if (d->sel_mode != None)
        repopulate_rates (d, d->sel_mode);
}

static void
populate_outputs (DisplayData *d)
{
    d->updating = TRUE;
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (d->monitor_combo));
    d->sel_output = None;

    for (int i = 0; i < d->res->noutput; i++) {
        XRROutputInfo *oi = XRRGetOutputInfo (d->xdpy, d->res, d->res->outputs[i]);
        if (!oi) continue;

        if (oi->connection == RR_Connected) {
            gchar *id = g_strdup_printf ("%lu", (gulong) d->res->outputs[i]);
            gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->monitor_combo), id, oi->name);
            CC_LOG ("connected output %s (%s)", oi->name, id);

            if (d->sel_output == None) {
                d->sel_output = d->res->outputs[i];
                gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->monitor_combo), id);
            }
            g_free (id);
        }

        XRRFreeOutputInfo (oi);
    }

    d->updating = FALSE;

    if (d->sel_output != None)
        populate_resolutions (d);
}

static void
refresh_resources (DisplayData *d)
{
    if (d->res) XRRFreeScreenResources (d->res);
    d->res = XRRGetScreenResourcesCurrent (d->xdpy, d->root);
    rebuild_mode_index (d);
}

/* ------------------------------------------------------------------ */
/*  Handlers                                                            */
/* ------------------------------------------------------------------ */

static void
on_monitor_changed (GtkComboBox *cb, DisplayData *d)
{
    if (d->updating) return;

    const gchar *id = gtk_combo_box_get_active_id (cb);
    if (!id) return;

    d->sel_output = (RROutput) g_ascii_strtoull (id, NULL, 10);
    populate_resolutions (d);
}

static void
on_res_changed (GtkComboBox *cb, DisplayData *d)
{
    if (d->updating) return;

    const gchar *id = gtk_combo_box_get_active_id (cb);
    unsigned int w = 0, h = 0;
    if (!id || sscanf (id, "%ux%u", &w, &h) != 2) return;

    XRROutputInfo *oi = XRRGetOutputInfo (d->xdpy, d->res, d->sel_output);
    if (!oi) return;

    for (int i = 0; i < oi->nmode; i++) {
        XRRModeInfo *mi = lookup_mode (d, oi->modes[i]);
        if (!mi || mi->width != w || mi->height != h) continue;

        d->sel_mode = mi->id;
        repopulate_rates (d, d->sel_mode);
        break;
    }

    XRRFreeOutputInfo (oi);
}

static void
on_rate_changed (GtkComboBox *cb, DisplayData *d)
{
    if (d->updating) return;

    const gchar *id = gtk_combo_box_get_active_id (cb);
    if (id) d->sel_mode = (RRMode) g_ascii_strtoull (id, NULL, 10);
}

static void
on_rotate_changed (GtkComboBox *cb, DisplayData *d)
{
    static const Rotation rotations[] = {
        RR_Rotate_0, RR_Rotate_90, RR_Rotate_180, RR_Rotate_270
    };

    if (d->updating) return;

    gint idx = gtk_combo_box_get_active (cb);
    if (idx >= 0 && (gsize) idx < G_N_ELEMENTS (rotations))
        d->sel_rotation = rotations[idx];
}

static void
on_primary_clicked (GtkButton *btn G_GNUC_UNUSED, DisplayData *d)
{
    if (d->sel_output == None) return;

    XRRSetOutputPrimary (d->xdpy, d->root, d->sel_output);
    XFlush (d->xdpy);
}

static RRCrtc
find_free_crtc (DisplayData *d)
{
    for (int i = 0; i < d->res->ncrtc; i++) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo (d->xdpy, d->res, d->res->crtcs[i]);
        if (!ci) continue;

        gboolean free_crtc = (ci->noutput == 0);
        RRCrtc crtc = d->res->crtcs[i];
        XRRFreeCrtcInfo (ci);

        if (free_crtc) return crtc;
    }
    return None;
}

static void
on_apply_clicked (GtkButton *btn G_GNUC_UNUSED, DisplayData *d)
{
    if (d->sel_output == None || d->sel_mode == None) return;

    XRROutputInfo *oi = XRRGetOutputInfo (d->xdpy, d->res, d->sel_output);
    if (!oi) return;

    RRCrtc crtc = oi->crtc;
    XRRFreeOutputInfo (oi);

    if (crtc == None)
        crtc = find_free_crtc (d);

    if (crtc == None) {
        g_warning ("cc-display: no CRTC available for the selected output");
        return;
    }

    XRRCrtcInfo *ci = XRRGetCrtcInfo (d->xdpy, d->res, crtc);
    if (!ci) return;

    Status st = XRRSetCrtcConfig (d->xdpy, d->res, crtc, CurrentTime,
                                  ci->x, ci->y, d->sel_mode, d->sel_rotation,
                                  &d->sel_output, 1);
    XRRFreeCrtcInfo (ci);
    XFlush (d->xdpy);

    CC_LOG ("XRRSetCrtcConfig returned %d", (int) st);

    /* The server's view of the screen has changed, so the cached resources
     * and the mode index derived from them are now stale. */
    refresh_resources (d);
    if (d->res) populate_outputs (d);
}

static void
on_scale_changed (GtkComboBox *cb, DisplayData *d)
{
    if (d->updating || !d->iface_s) return;

    /* Index 0 is 1x, index 1 is 2x. */
    guint factor = (gtk_combo_box_get_active (cb) == 1) ? 2 : 1;
    g_settings_set_uint (d->iface_s, KEY_SCALING, factor);
}

/* ------------------------------------------------------------------ */
/*  Teardown                                                            */
/* ------------------------------------------------------------------ */

static void
display_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, DisplayData *d)
{
    g_clear_pointer (&d->mode_index, g_hash_table_destroy);
    if (d->res) XRRFreeScreenResources (d->res);
    g_clear_object (&d->iface_s);
    g_free (d);
}

/* ------------------------------------------------------------------ */
/*  Public                                                              */
/* ------------------------------------------------------------------ */

GtkWidget *
cc_display_panel_new (void)
{
    DisplayData *d = g_new0 (DisplayData, 1);

    d->xdpy         = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
    d->root         = DefaultRootWindow (d->xdpy);
    d->sel_rotation = RR_Rotate_0;
    d->sel_output   = None;
    d->sel_mode     = None;

    refresh_resources (d);

    if (cc_schema_exists (IFACE_SCHEMA))
        d->iface_s = g_settings_new (IFACE_SCHEMA);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (display_panel_destroyed), d);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("DISPLAY")), FALSE, FALSE, 0);
    GtkGrid *grid = GTK_GRID (cc_page_add_grid (page));

    d->monitor_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (grid, 0, _("Monitor"), d->monitor_combo);

    d->res_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (grid, 1, _("Resolution"), d->res_combo);

    d->rate_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (grid, 2, _("Refresh rate"), d->rate_combo);

    d->rotate_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->rotate_combo), _("Normal"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->rotate_combo), "90\xc2\xb0");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->rotate_combo), "180\xc2\xb0");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->rotate_combo), "270\xc2\xb0");
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->rotate_combo), 0);
    cc_grid_attach_row (grid, 3, _("Rotation"), d->rotate_combo);

    d->scale_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->scale_combo), _("100% (1\xc3\x97)"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->scale_combo), _("200% (2\xc3\x97)"));
    if (d->iface_s) {
        guint factor = g_settings_get_uint (d->iface_s, KEY_SCALING);
        gtk_combo_box_set_active (GTK_COMBO_BOX (d->scale_combo), factor > 1 ? 1 : 0);
    } else {
        gtk_combo_box_set_active (GTK_COMBO_BOX (d->scale_combo), 0);
        gtk_widget_set_sensitive (d->scale_combo, FALSE);
    }
    cc_grid_attach_row (grid, 4, _("Scale"), d->scale_combo);

    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top (btn_box, 16);
    d->primary_btn = gtk_button_new_with_label (_("Set as primary"));
    d->apply_btn   = gtk_button_new_with_label (_("Apply"));
    gtk_style_context_add_class (gtk_widget_get_style_context (d->apply_btn),
                                 "suggested-action");
    gtk_box_pack_start (GTK_BOX (btn_box), d->primary_btn, FALSE, FALSE, 0);
    gtk_box_pack_end   (GTK_BOX (btn_box), d->apply_btn,   FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), btn_box, FALSE, FALSE, 0);

    g_signal_connect (d->monitor_combo, "changed", G_CALLBACK (on_monitor_changed), d);
    g_signal_connect (d->res_combo,     "changed", G_CALLBACK (on_res_changed),     d);
    g_signal_connect (d->rate_combo,    "changed", G_CALLBACK (on_rate_changed),    d);
    g_signal_connect (d->rotate_combo,  "changed", G_CALLBACK (on_rotate_changed),  d);
    g_signal_connect (d->scale_combo,   "changed", G_CALLBACK (on_scale_changed),   d);
    g_signal_connect (d->primary_btn,   "clicked", G_CALLBACK (on_primary_clicked), d);
    g_signal_connect (d->apply_btn,     "clicked", G_CALLBACK (on_apply_clicked),   d);

    if (d->res) {
        populate_outputs (d);
    } else {
        g_warning ("cc-display: XRandR reported no screen resources");
        gtk_widget_set_sensitive (GTK_WIDGET (grid), FALSE);
        gtk_widget_set_sensitive (btn_box, FALSE);
    }

    gtk_widget_show_all (scroll);
    return scroll;
}
