/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-background.c — Desktop Background settings panel
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
 * Wallpaper list/thumbnail backend vendored from mate-control-center:
 *   mate-wp-item.c / mate-wp-info.c / mate-wp-xml.c
 *   Copyright (C) 2003-2006 Novell, Inc.  Authors: Rodney Dawes
 *   Copyright (C) 2007-2008 The GNOME Foundation
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/mate-desktop/mate-control-center
 *
 * MateBG / MateDesktopThumbnailFactory from libmate-desktop:
 *   Copyright (C) 2000-2001 Ximian, Inc.
 *   Copyright (C) 2014-2021 MATE Developers
 *   Licensed under the GNU Lesser General Public License v2 or later.
 *   https://github.com/mate-desktop/mate-desktop
 * -----------------------------------------------------------------------
 *
 * Layout ported from the UKUI wallpaper plugin:
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
 *   - Wallpaper loading is a single idle pass over the hash; upstream
 *     queued one idle source and one heap job per wallpaper.
 *   - The five background keys are written by one apply_background()
 *     function; the same sequence appeared at three call sites.
 *   - g_settings_delay + g_settings_apply replaces delay + sync.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-background"

#include <string.h>
#include <stdlib.h>

#include "cc-common.h"
#include "cc-panels.h"
#include "appearance.h"
#include "mate-wp-item.h"
#include "mate-wp-xml.h"

#define THUMB_WIDTH    160
#define THUMB_HEIGHT    90
#define PREVIEW_WIDTH  320
#define PREVIEW_HEIGHT 180

#define NO_PICTURE "(none)"

static const gchar *const PRESET_COLORS[] = {
    "#2d7d9a", "#018574", "#107c10", "#10893e", "#038387",
    "#486860", "#525e54", "#7e735f", "#4c4a48", "#000000",
    "#ff8c00", "#e81123", "#d13438", "#c30052", "#bf0077",
    "#9a0089", "#881798", "#744da9", "#8764b8", "#e9e9e9",
    NULL
};

/* Placement values are written into the combo as their integer enum value
 * so that reading the selection back is a single atoi, not a table search. */
static const struct { MateBGPlacement value; const gchar *label; } PLACEMENTS[] = {
    { MATE_BG_PLACEMENT_ZOOMED,      N_("Zoom")   },
    { MATE_BG_PLACEMENT_FILL_SCREEN, N_("Fill")   },
    { MATE_BG_PLACEMENT_CENTERED,    N_("Center") },
    { MATE_BG_PLACEMENT_SCALED,      N_("Scale")  },
    { MATE_BG_PLACEMENT_TILED,       N_("Tile")   },
    { MATE_BG_PLACEMENT_SPANNED,     N_("Span")   },
};

typedef enum { PAGE_PICTURE = 0, PAGE_COLOR = 1 } BgPage;

typedef struct {
    AppearanceData ad;

    GtkWidget *preview_stack;
    GtkWidget *preview_image;
    GtkWidget *preview_color_box;

    GtkWidget *form_combo;
    GtkWidget *options_combo;
    GtkWidget *content_stack;

    GtkWidget *picture_flow;
    GtkWidget *color_flow;
    GtkWidget *custom_color_btn;

    GtkWidget *add_btn;
    GtkWidget *remove_btn;

    MateWPItem *selected;

    /* TRUE until the initial wallpaper scan finishes; suppresses the
     * GSettings change handler reacting to our own writes. */
    gboolean loading;
} BgData;

/* ------------------------------------------------------------------ */
/*  GSettings write                                                     */
/* ------------------------------------------------------------------ */

/*
 * Single writer for the five background keys. Picture selection, colour
 * swatches and the custom colour button all route through it, so the key
 * set can never drift between call sites.
 */
static void
apply_background (BgData *d,
                  const gchar     *filename,   /* NULL or "" clears the picture */
                  MateBGPlacement  placement,
                  MateBGColorType  shading,
                  const gchar     *pcolor,
                  const gchar     *scolor)
{
    GSettings *s = d->ad.wp_settings;
    gboolean has_picture = filename && *filename && strcmp (filename, NO_PICTURE) != 0;

    CC_LOG ("apply_background: file='%s' placement=%d shading=%d pcolor=%s",
            has_picture ? filename : "(cleared)", placement, shading, pcolor);

    g_settings_delay (s);
    g_settings_set_string (s, WP_FILE_KEY,    has_picture ? filename : "");
    g_settings_set_enum   (s, WP_OPTIONS_KEY, placement);
    g_settings_set_enum   (s, WP_SHADING_KEY, shading);
    g_settings_set_string (s, WP_PCOLOR_KEY,  pcolor);
    g_settings_set_string (s, WP_SCOLOR_KEY,  scolor);
    g_settings_apply (s);
}

static void
apply_item (BgData *d, MateWPItem *item)
{
    gchar *pcolor = gdk_rgba_to_string (item->pcolor);
    gchar *scolor = gdk_rgba_to_string (item->scolor);

    /* A zero placement means the item carries no stored preference;
     * zoomed is the least-bad default for arbitrary photographs. */
    MateBGPlacement placement = item->options ? item->options
                                              : MATE_BG_PLACEMENT_ZOOMED;

    apply_background (d, item->filename, placement, item->shade_type, pcolor, scolor);

    g_free (pcolor);
    g_free (scolor);
}

static void
apply_solid_color (BgData *d, const gchar *hex)
{
    apply_background (d, NULL, MATE_BG_PLACEMENT_ZOOMED,
                      MATE_BG_COLOR_SOLID, hex, hex);
}

/* ------------------------------------------------------------------ */
/*  Preview                                                             */
/* ------------------------------------------------------------------ */

static void
update_picture_preview (BgData *d, MateWPItem *item)
{
    GdkPixbuf *pb = mate_wp_item_get_thumbnail (item, d->ad.thumb_factory,
                                                PREVIEW_WIDTH, PREVIEW_HEIGHT);
    if (pb) {
        gtk_image_set_from_pixbuf (GTK_IMAGE (d->preview_image), pb);
        g_object_unref (pb);
    }

    gtk_stack_set_visible_child_name (GTK_STACK (d->preview_stack), "picture");

    gchar *placement_id = g_strdup_printf ("%d", (int) item->options);
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->options_combo), placement_id);
    g_free (placement_id);
}

static void
update_color_preview (BgData *d, const gchar *hex)
{
    gchar *css = g_strdup_printf ("* { background-color: %s; border-radius: 6px; }", hex);

    GtkCssProvider *prov = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (prov, css, -1, NULL);
    gtk_style_context_add_provider (
        gtk_widget_get_style_context (d->preview_color_box),
        GTK_STYLE_PROVIDER (prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (prov);
    g_free (css);

    gtk_stack_set_visible_child_name (GTK_STACK (d->preview_stack), "color");
}

/* ------------------------------------------------------------------ */
/*  Picture grid                                                        */
/* ------------------------------------------------------------------ */

static void
select_item (BgData *d, MateWPItem *item)
{
    d->selected = item;
    update_picture_preview (d, item);
    gtk_widget_set_sensitive (d->remove_btn, strcmp (item->filename, NO_PICTURE) != 0);
}

static GtkWidget *
make_wp_child (BgData *d, MateWPItem *item)
{
    GdkPixbuf *pb = mate_wp_item_get_thumbnail (item, d->ad.thumb_factory,
                                                THUMB_WIDTH, THUMB_HEIGHT);
    GtkWidget *img = pb
        ? gtk_image_new_from_pixbuf (pb)
        : gtk_image_new_from_icon_name ("image-missing", GTK_ICON_SIZE_DIALOG);
    if (pb) g_object_unref (pb);

    gtk_widget_set_tooltip_text (img, item->name ? item->name : item->filename);

    GtkWidget *btn = gtk_button_new ();
    gtk_button_set_relief (GTK_BUTTON (btn), GTK_RELIEF_NONE);
    gtk_container_add (GTK_CONTAINER (btn), img);

    GtkWidget *child = gtk_flow_box_child_new ();
    gtk_container_add (GTK_CONTAINER (child), btn);

    /* The item pointer lives in the hash table, which outlives the grid. */
    g_object_set_data (G_OBJECT (child), "wp-item", item);
    return child;
}

static MateWPItem *
child_item (GtkWidget *child)
{
    return g_object_get_data (G_OBJECT (child), "wp-item");
}

static void
on_picture_child_activated (GtkFlowBox *fb G_GNUC_UNUSED,
                            GtkFlowBoxChild *child, BgData *d)
{
    MateWPItem *item = child_item (GTK_WIDGET (child));
    if (!item) return;

    select_item (d, item);
    apply_item (d, item);
}

static void
on_options_changed (GtkComboBox *cb, BgData *d)
{
    if (d->loading || !d->selected) return;

    const gchar *id = gtk_combo_box_get_active_id (cb);
    if (!id) return;

    d->selected->options = (MateBGPlacement) atoi (id);
    apply_item (d, d->selected);
}

/* ------------------------------------------------------------------ */
/*  Colour page                                                         */
/* ------------------------------------------------------------------ */

static void
on_color_swatch_clicked (GtkButton *btn, BgData *d)
{
    const gchar *hex = g_object_get_data (G_OBJECT (btn), "color-hex");
    if (!hex) return;

    update_color_preview (d, hex);
    apply_solid_color (d, hex);
}

static void
on_custom_color_set (GtkColorButton *btn, BgData *d)
{
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (btn), &rgba);

    gchar *hex = gdk_rgba_to_string (&rgba);
    update_color_preview (d, hex);
    apply_solid_color (d, hex);
    g_free (hex);
}

/* ------------------------------------------------------------------ */
/*  Mode combo                                                          */
/* ------------------------------------------------------------------ */

static void
on_form_changed (GtkComboBox *cb, BgData *d)
{
    BgPage page = (BgPage) gtk_combo_box_get_active (cb);

    gtk_stack_set_visible_child_name (GTK_STACK (d->content_stack),
                                      page == PAGE_COLOR ? "color" : "picture");
    gtk_widget_set_sensitive (d->add_btn, page == PAGE_PICTURE);
    gtk_widget_set_sensitive (d->remove_btn,
        page == PAGE_PICTURE && d->selected &&
        strcmp (d->selected->filename, NO_PICTURE) != 0);

    if (page == PAGE_COLOR) {
        gchar *hex = g_settings_get_string (d->ad.wp_settings, WP_PCOLOR_KEY);
        update_color_preview (d, (hex && *hex) ? hex : "#000000");
        g_free (hex);
    } else if (d->selected) {
        update_picture_preview (d, d->selected);
    }
}

/* ------------------------------------------------------------------ */
/*  Add / Remove                                                        */
/* ------------------------------------------------------------------ */

static void
insert_item_child (BgData *d, MateWPItem *item)
{
    GtkWidget *child = make_wp_child (d, item);
    gtk_flow_box_insert (GTK_FLOW_BOX (d->picture_flow), child, -1);
    gtk_widget_show_all (child);
}

static void
on_add_clicked (GtkButton *btn G_GNUC_UNUSED, BgData *d)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new (
        _("Add Wallpaper"),
        GTK_WINDOW (gtk_widget_get_toplevel (d->picture_flow)),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Open"),   GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dlg), TRUE);

    GtkFileFilter *ff = gtk_file_filter_new ();
    gtk_file_filter_set_name (ff, _("Images"));
    gtk_file_filter_add_pixbuf_formats (ff);
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dlg), ff);

    const gchar *pics = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
    if (pics) gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dlg), pics);

    if (gtk_dialog_run (GTK_DIALOG (dlg)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy (dlg);
        return;
    }

    GSList *files = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (dlg));

    for (GSList *l = files; l; l = l->next) {
        gchar *path = l->data;

        if (g_hash_table_lookup (d->ad.wp_hash, path)) {
            CC_LOG ("add: '%s' already listed", path);
            g_free (path);
            continue;
        }

        MateWPItem *item = mate_wp_item_new (path, d->ad.wp_hash, d->ad.thumb_factory);
        if (!item) {
            g_warning ("cc-background: '%s' is not a usable image", path);
            g_free (path);
            continue;
        }

        if (!item->options)
            item->options = MATE_BG_PLACEMENT_ZOOMED;

        insert_item_child (d, item);
        select_item (d, item);
        apply_item (d, item);

        g_free (path);
    }

    g_slist_free (files);
    gtk_widget_destroy (dlg);
}

static void
on_remove_clicked (GtkButton *btn G_GNUC_UNUSED, BgData *d)
{
    if (!d->selected || !strcmp (d->selected->filename, NO_PICTURE)) return;

    d->selected->deleted = TRUE;

    GList *children = gtk_container_get_children (GTK_CONTAINER (d->picture_flow));
    for (GList *l = children; l; l = l->next) {
        if (child_item (l->data) == d->selected) {
            gtk_widget_destroy (GTK_WIDGET (l->data));
            break;
        }
    }
    g_list_free (children);

    d->selected = NULL;
    gtk_widget_set_sensitive (d->remove_btn, FALSE);
}

/* ------------------------------------------------------------------ */
/*  External GSettings change                                           */
/* ------------------------------------------------------------------ */

static void
on_bg_file_changed (GSettings *s G_GNUC_UNUSED, gchar *key G_GNUC_UNUSED, BgData *d)
{
    if (d->loading) return;

    gchar *uri = g_settings_get_string (d->ad.wp_settings, WP_FILE_KEY);
    if (uri && *uri) {
        MateWPItem *item = g_hash_table_lookup (d->ad.wp_hash, uri);
        if (item) select_item (d, item);
    }
    g_free (uri);
}

/* ------------------------------------------------------------------ */
/*  Teardown                                                            */
/* ------------------------------------------------------------------ */

static void
bg_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, BgData *d)
{
    mate_wp_xml_save_list (&d->ad);

    /* The hash owns the items; it is destroyed exactly once, here. */
    g_clear_pointer (&d->ad.wp_hash, g_hash_table_destroy);
    g_clear_object (&d->ad.wp_settings);
    g_clear_object (&d->ad.thumb_factory);
    g_free (d);
}

/* ------------------------------------------------------------------ */
/*  Deferred load                                                       */
/* ------------------------------------------------------------------ */

/*
 * One idle source builds the whole grid. The previous revision queued a
 * separate g_idle_add per wallpaper, which made the number of main-loop
 * sources proportional to the library size for no measurable benefit.
 */
static gboolean
load_wallpapers_idle (gpointer user_data)
{
    BgData *d = user_data;

    d->ad.thumb_width  = THUMB_WIDTH;
    d->ad.thumb_height = THUMB_HEIGHT;

    mate_wp_xml_load_list (&d->ad);
    CC_LOG ("loaded %u wallpapers", g_hash_table_size (d->ad.wp_hash));

    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init (&it, d->ad.wp_hash);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        MateWPItem *item = value;
        if (!item->deleted)
            insert_item_child (d, item);
    }

    gchar *current = g_settings_get_string (d->ad.wp_settings, WP_FILE_KEY);
    if (current && *current) {
        MateWPItem *item = g_hash_table_lookup (d->ad.wp_hash, current);
        if (item) select_item (d, item);
        else CC_LOG ("current wallpaper '%s' is not in the library", current);
    }
    g_free (current);

    d->loading = FALSE;
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/*  Public                                                              */
/* ------------------------------------------------------------------ */

GtkWidget *
cc_background_panel_new (void)
{
    BgData *d  = g_new0 (BgData, 1);
    d->loading = TRUE;

    d->ad.wp_settings   = g_settings_new (WP_SCHEMA);
    d->ad.wp_hash       = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                 (GDestroyNotify) mate_wp_item_free);
    d->ad.thumb_factory = mate_desktop_thumbnail_factory_new (
                              MATE_DESKTOP_THUMBNAIL_SIZE_NORMAL);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (bg_panel_destroyed), d);

    GtkWidget *title = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (title),
        "<span weight=\"bold\" size=\"large\">Desktop Background</span>");
    gtk_widget_set_halign (title, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom (title, 12);
    gtk_box_pack_start (GTK_BOX (page), title, FALSE, FALSE, 0);

    /* Preview */
    d->preview_stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (d->preview_stack),
                                   GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    d->preview_image = gtk_image_new ();
    gtk_widget_set_size_request (d->preview_image, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    GtkWidget *pic_frame = gtk_frame_new (NULL);
    gtk_container_add (GTK_CONTAINER (pic_frame), d->preview_image);
    gtk_stack_add_named (GTK_STACK (d->preview_stack), pic_frame, "picture");

    d->preview_color_box = gtk_drawing_area_new ();
    gtk_widget_set_size_request (d->preview_color_box, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    gtk_stack_add_named (GTK_STACK (d->preview_stack), d->preview_color_box, "color");

    GtkWidget *preview_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (preview_hbox), d->preview_stack, FALSE, FALSE, 0);
    gtk_widget_set_margin_bottom (preview_hbox, 16);
    gtk_box_pack_start (GTK_BOX (page), preview_hbox, FALSE, FALSE, 0);

    /* Options */
    GtkGrid *opt_grid = GTK_GRID (cc_page_add_grid (page));
    gtk_widget_set_margin_bottom (GTK_WIDGET (opt_grid), 8);

    d->form_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->form_combo), _("Picture"));
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (d->form_combo), _("Color"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->form_combo), PAGE_PICTURE);
    cc_grid_attach_row (opt_grid, 0, _("Type:"), d->form_combo);

    d->options_combo = gtk_combo_box_text_new ();
    for (gsize i = 0; i < G_N_ELEMENTS (PLACEMENTS); i++) {
        gchar *id = g_strdup_printf ("%d", (int) PLACEMENTS[i].value);
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->options_combo),
                                   id, _(PLACEMENTS[i].label));
        g_free (id);
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->options_combo), 0);
    cc_grid_attach_row (opt_grid, 1, _("Placement:"), d->options_combo);

    /* Content stack */
    d->content_stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (d->content_stack),
                                   GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_box_pack_start (GTK_BOX (page), d->content_stack, TRUE, TRUE, 0);

    GtkWidget *pic_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (pic_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (pic_scroll, -1, 300);
    d->picture_flow = gtk_flow_box_new ();
    gtk_flow_box_set_homogeneous           (GTK_FLOW_BOX (d->picture_flow), TRUE);
    gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (d->picture_flow), 2);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (d->picture_flow), 6);
    gtk_flow_box_set_selection_mode        (GTK_FLOW_BOX (d->picture_flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_row_spacing           (GTK_FLOW_BOX (d->picture_flow), 6);
    gtk_flow_box_set_column_spacing        (GTK_FLOW_BOX (d->picture_flow), 6);
    gtk_container_add (GTK_CONTAINER (pic_scroll), d->picture_flow);
    gtk_stack_add_named (GTK_STACK (d->content_stack), pic_scroll, "picture");

    /* Colour page */
    GtkWidget *color_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top (color_page, 8);
    d->color_flow = gtk_flow_box_new ();
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (d->color_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing    (GTK_FLOW_BOX (d->color_flow), 6);
    gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (d->color_flow), 6);

    for (int i = 0; PRESET_COLORS[i]; i++) {
        const gchar *hex = PRESET_COLORS[i];
        GtkWidget *btn = gtk_button_new ();
        gtk_widget_set_size_request (btn, 48, 48);
        g_object_set_data (G_OBJECT (btn), "color-hex", (gpointer) hex);

        gchar *css = g_strdup_printf (
            "button { background: %s; border-radius: 4px; "
            "border: 1px solid alpha(#000,0.2); "
            "min-width:0; min-height:0; padding:0; }", hex);
        GtkCssProvider *prov = gtk_css_provider_new ();
        gtk_css_provider_load_from_data (prov, css, -1, NULL);
        gtk_style_context_add_provider (gtk_widget_get_style_context (btn),
                                        GTK_STYLE_PROVIDER (prov),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref (prov);
        g_free (css);

        GtkWidget *child = gtk_flow_box_child_new ();
        gtk_container_add (GTK_CONTAINER (child), btn);
        gtk_flow_box_insert (GTK_FLOW_BOX (d->color_flow), child, -1);
        g_signal_connect (btn, "clicked", G_CALLBACK (on_color_swatch_clicked), d);
    }
    gtk_box_pack_start (GTK_BOX (color_page), d->color_flow, FALSE, TRUE, 0);

    GtkWidget *custom_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    d->custom_color_btn = gtk_color_button_new ();
    gtk_box_pack_start (GTK_BOX (custom_row), gtk_label_new (_("Custom color:")),
                        FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (custom_row), d->custom_color_btn, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (color_page), custom_row, FALSE, FALSE, 0);
    gtk_stack_add_named (GTK_STACK (d->content_stack), color_page, "color");

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top (btn_box, 12);
    d->add_btn    = gtk_button_new_with_label (_("Add Picture\xe2\x80\xa6"));
    d->remove_btn = gtk_button_new_with_label (_("Remove Picture"));
    gtk_widget_set_sensitive (d->remove_btn, FALSE);
    gtk_box_pack_start (GTK_BOX (btn_box), d->add_btn,    FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (btn_box), d->remove_btn, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), btn_box, FALSE, FALSE, 0);

    g_signal_connect (d->form_combo,       "changed",         G_CALLBACK (on_form_changed),            d);
    g_signal_connect (d->options_combo,    "changed",         G_CALLBACK (on_options_changed),         d);
    g_signal_connect (d->picture_flow,     "child-activated", G_CALLBACK (on_picture_child_activated), d);
    g_signal_connect (d->add_btn,          "clicked",         G_CALLBACK (on_add_clicked),             d);
    g_signal_connect (d->remove_btn,       "clicked",         G_CALLBACK (on_remove_clicked),          d);
    g_signal_connect (d->custom_color_btn, "color-set",       G_CALLBACK (on_custom_color_set),        d);
    g_signal_connect (d->ad.wp_settings,   "changed::" WP_FILE_KEY,
                      G_CALLBACK (on_bg_file_changed), d);

    g_idle_add (load_wallpapers_idle, d);

    gtk_widget_show_all (scroll);
    return scroll;
}
