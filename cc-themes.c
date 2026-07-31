/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-themes.c — Appearance & Themes settings panel
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
 * Cursor thumbnail rendering via libXcursor:
 *   Copyright (C) 2002 Keith Packard
 *   Licensed under the MIT/X11 License.
 *
 * Theme/icon/cursor discovery adapted from mate-control-center
 * appearance-themes.c / appearance-style.c:
 *   Copyright (C) 2007, 2010 The GNOME Foundation
 *   Written by Thomas Wood, Jens Granseuer
 *   Licensed under the GNU General Public License v2.
 *   https://github.com/mate-desktop/mate-control-center
 *
 * Layout ported from the UKUI theme plugin:
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
 *   - select_current_in_flow() rewritten: an unbraced `if (!found)`
 *     before g_list_free(children) leaked the list whenever the theme
 *     was found.
 *   - collect_themes() stores the strdup'ed name in the `seen` table;
 *     g_dir_read_name's buffer is invalidated by the next call.
 *   - Grid population is one idle source per grid, selecting immediately
 *     afterwards. Upstream queued one idle per theme plus a 300 ms
 *     timeout, racing thumbnail generation.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-themes"

#include <string.h>
#include <math.h>
#include <X11/Xcursor/Xcursor.h>

#include "cc-common.h"
#include "cc-panels.h"

#define KEY_GTK_THEME     "gtk-theme"
#define KEY_ICON_THEME    "icon-theme"
#define KEY_CURSOR_THEME  "cursor-theme"
#define KEY_CURSOR_SIZE   "cursor-size"
#define KEY_FONT          "font-name"
#define KEY_DOC_FONT      "document-font-name"
#define KEY_MONO_FONT     "monospace-font-name"
#define KEY_ANTIALIASING  "font-antialiasing"
#define KEY_HINTING       "font-hinting"
#define KEY_RGBA_ORDER    "font-rgba-order"
#define KEY_WM_THEME      "theme"
#define KEY_TITLEBAR_FONT "titlebar-font"
#define KEY_TITLEBAR_SYS  "titlebar-uses-system-font"

/* Preference lists, most specific first. mate-settings-daemon reads the
 * org.mate.* schemas on a MATE session; the GNOME schemas are the fallback
 * for sessions that ship only those. */
static const gchar *const IFACE_SCHEMAS[] = { "org.gnome.desktop.interface", NULL };
static const gchar *const THEME_SCHEMAS[] = { "org.mate.interface",
                                              "org.gnome.desktop.interface", NULL };
static const gchar *const MOUSE_SCHEMAS[] = { "org.mate.peripherals-mouse",
                                              "org.gnome.desktop.interface", NULL };
static const gchar *const WM_SCHEMAS[]    = { "org.mate.Marco.general",
                                              "org.gnome.desktop.wm.preferences", NULL };

#define THUMB_W 160
#define THUMB_H  90

typedef enum { THUMB_GTK = 0, THUMB_ICON, THUMB_CURSOR } ThumbKind;

typedef struct {
    GSettings *iface_s;   /* font rendering                    */
    GSettings *theme_s;   /* gtk/icon theme and interface fonts */
    GSettings *wm_s;      /* window border theme and titlebar   */
    GSettings *mouse_s;   /* cursor theme                       */

    GtkWidget *gtk_flow;
    GtkWidget *icon_flow;
    GtkWidget *cursor_flow;
    GtkWidget *wm_combo;

    GtkWidget *ui_font_btn;
    GtkWidget *doc_font_btn;
    GtkWidget *mono_font_btn;
    GtkWidget *title_font_btn;
    GtkWidget *title_system_sw;

    GtkWidget *aa_combo;
    GtkWidget *hint_combo;
    GtkWidget *rgba_combo;
    GtkWidget *cursor_size_spin;

    gboolean   updating;
} ThemesData;

/* The cursor-theme key name differs between the MATE mouse schema and the
 * GNOME interface fallback, so it is resolved once at construction. */
static const gchar *cursor_key = KEY_CURSOR_THEME;

/* ================================================================== */
/*  Discovery                                                           */
/* ================================================================== */

static gboolean
is_gtk3_theme (const gchar *dir, const gchar *name)
{
    gchar *p = g_build_filename (dir, name, "gtk-3.0", NULL);
    gboolean ok = g_file_test (p, G_FILE_TEST_IS_DIR);
    g_free (p);
    return ok;
}

static gboolean
is_wm_theme (const gchar *dir, const gchar *name)
{
    gchar *metacity = g_build_filename (dir, name, "metacity-1", NULL);
    gchar *marco    = g_build_filename (dir, name, "marco", NULL);
    gboolean ok = g_file_test (metacity, G_FILE_TEST_IS_DIR) ||
                  g_file_test (marco,    G_FILE_TEST_IS_DIR);
    g_free (metacity);
    g_free (marco);
    return ok;
}

/*
 * Cursor-only themes also ship an index.theme with an [Icon Theme] group,
 * so the group alone does not distinguish them. A real icon theme has at
 * least one size directory besides cursors/.
 */
static gboolean
has_icon_size_dir (const gchar *theme_dir)
{
    GDir *dir = g_dir_open (theme_dir, 0, NULL);
    if (!dir) return FALSE;

    const gchar *entry;
    gboolean found = FALSE;

    while (!found && (entry = g_dir_read_name (dir))) {
        if (!strcmp (entry, "cursors") ||
            !strcmp (entry, "index.theme") ||
            !strcmp (entry, "thumbnail.png"))
            continue;

        gchar *sub = g_build_filename (theme_dir, entry, NULL);
        found = g_file_test (sub, G_FILE_TEST_IS_DIR);
        g_free (sub);
    }

    g_dir_close (dir);
    return found;
}

static gboolean
is_icon_theme (const gchar *dir, const gchar *name)
{
    gchar *index = g_build_filename (dir, name, "index.theme", NULL);
    gchar *contents = NULL;
    gboolean ok = FALSE;

    if (g_file_get_contents (index, &contents, NULL, NULL)) {
        if (strstr (contents, "[Icon Theme]")) {
            gchar *theme_dir = g_build_filename (dir, name, NULL);
            ok = has_icon_size_dir (theme_dir);
            g_free (theme_dir);
        }
        g_free (contents);
    }

    g_free (index);
    return ok;
}

static gboolean
is_cursor_theme (const gchar *dir, const gchar *name)
{
    gchar *p = g_build_filename (dir, name, "cursors", NULL);
    gboolean ok = g_file_test (p, G_FILE_TEST_IS_DIR);
    g_free (p);
    return ok;
}

static GList *
collect_themes (const gchar *const *dirs,
                gboolean (*pred) (const gchar *, const gchar *))
{
    GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
    GList *out = NULL;

    for (int i = 0; dirs[i]; i++) {
        GDir *dir = g_dir_open (dirs[i], 0, NULL);
        if (!dir) continue;

        const gchar *entry;
        while ((entry = g_dir_read_name (dir))) {
            if (entry[0] == '.') continue;
            if (g_hash_table_contains (seen, entry)) continue;
            if (!pred (dirs[i], entry)) continue;

            /* Key must outlive g_dir_read_name's internal buffer. */
            gchar *name = g_strdup (entry);
            g_hash_table_add (seen, name);
            out = g_list_prepend (out, name);
        }
        g_dir_close (dir);
    }

    g_hash_table_destroy (seen);
    return g_list_sort (out, (GCompareFunc) g_strcmp0);
}

static const gchar *const *
theme_search_dirs (void)
{
    static gchar *dirs[4];

    if (!dirs[0]) {
        dirs[0] = g_strdup ("/usr/share/themes");
        dirs[1] = g_build_filename (g_get_home_dir (), ".themes", NULL);
        dirs[2] = g_build_filename (g_get_home_dir (), ".local/share/themes", NULL);
    }
    return (const gchar *const *) dirs;
}

static const gchar *const *
icon_search_dirs (void)
{
    static gchar *dirs[5];

    if (!dirs[0]) {
        dirs[0] = g_strdup ("/usr/share/icons");
        dirs[1] = g_build_filename (g_get_home_dir (), ".icons", NULL);
        dirs[2] = g_build_filename (g_get_home_dir (), ".local/share/icons", NULL);
        dirs[3] = g_strdup ("/usr/local/share/icons");
    }
    return (const gchar *const *) dirs;
}

/* ================================================================== */
/*  Thumbnails                                                          */
/* ================================================================== */

static void
theme_accent_rgb (const gchar *name, gdouble *r, gdouble *g, gdouble *b)
{
    guint32 hash = g_str_hash (name);
    gdouble hue  = (hash & 0xfff) / 4096.0;
    gdouble s = 0.40, v = 0.82;
    gdouble h6 = hue * 6.0;
    int     hi = (int) h6 % 6;
    gdouble f  = h6 - (int) h6;
    gdouble p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));

    switch (hi) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default:*r = v; *g = p; *b = q; break;
    }
}

static GdkPixbuf *
surface_to_pixbuf (cairo_surface_t *sf)
{
    GdkPixbuf *pb = gdk_pixbuf_get_from_surface (sf, 0, 0, THUMB_W, THUMB_H);
    cairo_surface_destroy (sf);
    return pb;
}

static GdkPixbuf *
make_gtk_theme_thumb (const gchar *name)
{
    gdouble r, g, b;
    theme_accent_rgb (name, &r, &g, &b);

    cairo_surface_t *sf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, THUMB_W, THUMB_H);
    cairo_t *cr = cairo_create (sf);

    cairo_set_source_rgb (cr, r * 0.9, g * 0.9, b * 0.9);
    cairo_paint (cr);

    cairo_set_source_rgb (cr, r * 0.6, g * 0.6, b * 0.6);
    cairo_rectangle (cr, 0, 0, THUMB_W, 22);
    cairo_fill (cr);

    cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    for (int i = 0; i < 3; i++) {
        cairo_arc (cr, THUMB_W - 10 - i * 14, 11, 4, 0, 2 * G_PI);
        cairo_fill (cr);
    }

    cairo_set_source_rgba (cr, 1, 1, 1, 0.18);
    cairo_rectangle (cr, 8, 30, THUMB_W - 16, 12); cairo_fill (cr);
    cairo_rectangle (cr, 8, 48, THUMB_W - 32, 10); cairo_fill (cr);
    cairo_rectangle (cr, 8, 64, THUMB_W - 24, 10); cairo_fill (cr);

    cairo_set_source_rgb (cr, r * 0.7, g * 0.7, b * 0.7);
    cairo_rectangle (cr, THUMB_W - 50, THUMB_H - 18, 42, 12);
    cairo_fill (cr);

    cairo_set_source_rgba (cr, 1, 1, 1, 0.9);
    cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, 9);
    cairo_move_to (cr, 8, 15);
    cairo_show_text (cr, name);

    cairo_destroy (cr);
    return surface_to_pixbuf (sf);
}

static GdkPixbuf *
make_icon_theme_thumb (const gchar *name)
{
    static const gchar *const probes[] = {
        "folder", "text-x-generic", "image-x-generic", "application-x-executable"
    };
    const gint sz = 36;
    const gint pos[4][2] = {
        { THUMB_W / 2 - sz - 4, THUMB_H / 2 - sz - 4 },
        { THUMB_W / 2 + 4,      THUMB_H / 2 - sz - 4 },
        { THUMB_W / 2 - sz - 4, THUMB_H / 2 + 4      },
        { THUMB_W / 2 + 4,      THUMB_H / 2 + 4      }
    };

    GtkIconTheme *theme = gtk_icon_theme_new ();
    gtk_icon_theme_set_custom_theme (theme, name);

    cairo_surface_t *sf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, THUMB_W, THUMB_H);
    cairo_t *cr = cairo_create (sf);
    cairo_set_source_rgb (cr, 0.94, 0.94, 0.94);
    cairo_paint (cr);

    for (gsize i = 0; i < G_N_ELEMENTS (probes); i++) {
        GdkPixbuf *icon = gtk_icon_theme_load_icon (theme, probes[i], sz,
                                                    GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        if (!icon) continue;

        gdk_cairo_set_source_pixbuf (cr, icon, pos[i][0], pos[i][1]);
        cairo_paint (cr);
        g_object_unref (icon);
    }

    g_object_unref (theme);
    cairo_destroy (cr);
    return surface_to_pixbuf (sf);
}

static XcursorImages *
load_cursor_images (const gchar *theme_name)
{
    static const gchar *const cursor_names[] = { "default", "left_ptr", "arrow" };
    const gchar *const *search = icon_search_dirs ();

    for (int di = 0; search[di]; di++) {
        for (gsize ci = 0; ci < G_N_ELEMENTS (cursor_names); ci++) {
            gchar *path = g_build_filename (search[di], theme_name, "cursors",
                                            cursor_names[ci], NULL);
            XcursorImages *imgs = XcursorFilenameLoadImages (path, 32);
            g_free (path);
            if (imgs) return imgs;
        }
    }
    return NULL;
}

/* XcursorImage stores premultiplied ARGB; GdkPixbuf expects straight RGBA. */
static GdkPixbuf *
cursor_image_to_pixbuf (const XcursorImage *img)
{
    GdkPixbuf *pb = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
                                    (int) img->width, (int) img->height);
    if (!pb) return NULL;

    guchar *data = gdk_pixbuf_get_pixels (pb);
    gint stride  = gdk_pixbuf_get_rowstride (pb);

    for (guint y = 0; y < img->height; y++) {
        for (guint x = 0; x < img->width; x++) {
            XcursorPixel px = img->pixels[y * img->width + x];
            guchar *p = data + y * stride + x * 4;
            guchar a = (px >> 24) & 0xff;

            p[0] = a ? (guchar) (((px >> 16) & 0xff) * 255u / a) : 0;
            p[1] = a ? (guchar) (((px >>  8) & 0xff) * 255u / a) : 0;
            p[2] = a ? (guchar) (( px        & 0xff) * 255u / a) : 0;
            p[3] = a;
        }
    }
    return pb;
}

static void
draw_fallback_arrow (cairo_t *cr)
{
    cairo_set_source_rgb (cr, 0.2, 0.2, 0.2);
    cairo_move_to (cr, THUMB_W / 2.0 - 14, THUMB_H / 2.0 - 18);
    cairo_line_to (cr, THUMB_W / 2.0 - 14, THUMB_H / 2.0 +  8);
    cairo_line_to (cr, THUMB_W / 2.0 -  4, THUMB_H / 2.0 +  2);
    cairo_line_to (cr, THUMB_W / 2.0 +  4, THUMB_H / 2.0 + 14);
    cairo_line_to (cr, THUMB_W / 2.0 + 10, THUMB_H / 2.0 +  8);
    cairo_line_to (cr, THUMB_W / 2.0 +  0, THUMB_H / 2.0 -  4);
    cairo_line_to (cr, THUMB_W / 2.0 +  8, THUMB_H / 2.0 - 10);
    cairo_close_path (cr);
    cairo_fill (cr);
}

static GdkPixbuf *
make_cursor_theme_thumb (const gchar *name)
{
    const gchar *const *search = icon_search_dirs ();

    /* Some themes ship a ready-made thumbnail; use it rather than
     * rasterising a cursor ourselves. */
    for (int i = 0; search[i]; i++) {
        gchar *path = g_build_filename (search[i], name, "thumbnail.png", NULL);
        GdkPixbuf *pb = g_file_test (path, G_FILE_TEST_EXISTS)
            ? gdk_pixbuf_new_from_file_at_scale (path, THUMB_W, THUMB_H, FALSE, NULL)
            : NULL;
        g_free (path);
        if (pb) return pb;
    }

    cairo_surface_t *sf = cairo_image_surface_create (CAIRO_FORMAT_RGB24, THUMB_W, THUMB_H);
    cairo_t *cr = cairo_create (sf);

    cairo_set_source_rgb (cr, 0.88, 0.88, 0.88);
    cairo_paint (cr);

    cairo_set_source_rgb (cr, 0.3, 0.3, 0.3);
    cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size (cr, 9);
    cairo_text_extents_t te;
    cairo_text_extents (cr, name, &te);
    cairo_move_to (cr, (THUMB_W - te.width) / 2.0, THUMB_H - 8);
    cairo_show_text (cr, name);

    XcursorImages *imgs = load_cursor_images (name);
    GdkPixbuf *cursor = NULL;

    if (imgs && imgs->nimage > 0 &&
        imgs->images[0]->width > 0 && imgs->images[0]->height > 0)
        cursor = cursor_image_to_pixbuf (imgs->images[0]);

    if (cursor) {
        gdouble scale = MIN (48.0 / gdk_pixbuf_get_width (cursor),
                             48.0 / gdk_pixbuf_get_height (cursor));
        gint dw = MAX (1, (gint) (gdk_pixbuf_get_width  (cursor) * scale));
        gint dh = MAX (1, (gint) (gdk_pixbuf_get_height (cursor) * scale));
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple (cursor, dw, dh, GDK_INTERP_BILINEAR);

        if (scaled) {
            gdk_cairo_set_source_pixbuf (cr, scaled,
                                         (THUMB_W - dw) / 2.0,
                                         (THUMB_H - dh) / 2.0 - 6);
            cairo_paint (cr);
            g_object_unref (scaled);
        }
        g_object_unref (cursor);
    } else {
        draw_fallback_arrow (cr);
    }

    if (imgs) XcursorImagesDestroy (imgs);

    cairo_destroy (cr);
    return surface_to_pixbuf (sf);
}

static GdkPixbuf *
make_thumb (ThumbKind kind, const gchar *name)
{
    switch (kind) {
    case THUMB_GTK:    return make_gtk_theme_thumb (name);
    case THUMB_ICON:   return make_icon_theme_thumb (name);
    case THUMB_CURSOR: return make_cursor_theme_thumb (name);
    }
    g_assert_not_reached ();
}

/* ================================================================== */
/*  Grids                                                               */
/* ================================================================== */

static GtkWidget *
make_theme_child (const gchar *name, GdkPixbuf *thumb)
{
    GtkWidget *card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start  (card, 4);
    gtk_widget_set_margin_end    (card, 4);
    gtk_widget_set_margin_top    (card, 4);
    gtk_widget_set_margin_bottom (card, 4);

    GtkWidget *img = thumb
        ? gtk_image_new_from_pixbuf (thumb)
        : gtk_image_new_from_icon_name ("preferences-desktop-theme", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request (img, THUMB_W, THUMB_H);

    GtkWidget *frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER (frame), img);

    GtkWidget *label = gtk_label_new (name);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 18);

    gtk_box_pack_start (GTK_BOX (card), frame, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (card), label, FALSE, FALSE, 0);

    GtkWidget *child = gtk_flow_box_child_new ();
    gtk_container_add (GTK_CONTAINER (child), card);
    g_object_set_data_full (G_OBJECT (child), "theme-name", g_strdup (name), g_free);
    return child;
}

static const gchar *
child_theme_name (GtkWidget *child)
{
    return g_object_get_data (G_OBJECT (child), "theme-name");
}

static const gchar *
selected_theme_name (GtkFlowBox *flow)
{
    GList *sel = gtk_flow_box_get_selected_children (flow);
    if (!sel) return NULL;

    const gchar *name = child_theme_name (GTK_WIDGET (sel->data));
    g_list_free (sel);
    return name;
}

static void
select_current_in_flow (GtkFlowBox *flow, GSettings *settings, const gchar *key)
{
    if (!settings) return;

    gchar *current = g_settings_get_string (settings, key);
    GList *children = gtk_container_get_children (GTK_CONTAINER (flow));

    for (GList *l = children; l; l = l->next) {
        if (g_strcmp0 (child_theme_name (l->data), current) != 0) continue;

        gtk_flow_box_select_child (flow, GTK_FLOW_BOX_CHILD (l->data));
        break;
    }

    g_list_free (children);
    g_free (current);
}

/* Allocations are only valid once GTK has laid the grid out, so the scroll
 * runs from a low-priority idle after population, not from a fixed delay. */
static gboolean
scroll_to_selected_idle (gpointer user_data)
{
    GtkFlowBox *flow = user_data;

    GList *sel = gtk_flow_box_get_selected_children (flow);
    if (!sel) return G_SOURCE_REMOVE;

    GtkWidget *child = GTK_WIDGET (sel->data);
    g_list_free (sel);

    GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (flow));
    while (parent && !GTK_IS_SCROLLED_WINDOW (parent))
        parent = gtk_widget_get_parent (parent);
    if (!parent) return G_SOURCE_REMOVE;

    GtkAllocation alloc;
    gtk_widget_get_allocation (child, &alloc);
    if (alloc.height > 0)
        gtk_adjustment_clamp_page (
            gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (parent)),
            alloc.y, alloc.y + alloc.height);

    return G_SOURCE_REMOVE;
}

typedef struct {
    ThemesData *d;
    GtkFlowBox *flow;
    GSettings  *settings;
    const gchar *key;      /* static string */
    ThumbKind   kind;
    GList      *names;     /* owned */
} PopulateJob;

/*
 * One idle source per grid, which builds every card and then selects the
 * active theme. The previous revision queued one idle per theme plus a
 * 300 ms timeout hoping the queue had drained by then; the selection was
 * therefore a race against thumbnail generation on slow machines.
 */
static gboolean
populate_flow_idle (gpointer user_data)
{
    PopulateJob *job = user_data;

    for (GList *l = job->names; l; l = l->next) {
        GdkPixbuf *thumb = make_thumb (job->kind, l->data);
        GtkWidget *child = make_theme_child (l->data, thumb);
        if (thumb) g_object_unref (thumb);

        gtk_flow_box_insert (job->flow, child, -1);
        gtk_widget_show_all (child);
    }

    job->d->updating = TRUE;
    select_current_in_flow (job->flow, job->settings, job->key);
    job->d->updating = FALSE;

    g_idle_add_full (G_PRIORITY_LOW, scroll_to_selected_idle, job->flow, NULL);

    g_list_free_full (job->names, g_free);
    g_free (job);
    return G_SOURCE_REMOVE;
}

static void
queue_populate (ThemesData *d, GtkWidget *flow, ThumbKind kind,
                GList *names, GSettings *settings, const gchar *key)
{
    PopulateJob *job = g_new0 (PopulateJob, 1);

    job->d        = d;
    job->flow     = GTK_FLOW_BOX (flow);
    job->settings = settings;
    job->key      = key;
    job->kind     = kind;
    job->names    = names;

    g_idle_add (populate_flow_idle, job);
}

/* ================================================================== */
/*  Handlers                                                            */
/* ================================================================== */

static void
store_selected_theme (ThemesData *d, GtkWidget *flow,
                      GSettings *settings, const gchar *key)
{
    if (d->updating || !settings) return;

    const gchar *name = selected_theme_name (GTK_FLOW_BOX (flow));
    if (!name) return;

    CC_LOG ("%s -> '%s'", key, name);
    g_settings_set_string (settings, key, name);
}

static void
on_gtk_theme_selected (GtkFlowBox *f G_GNUC_UNUSED,
                       GtkFlowBoxChild *c G_GNUC_UNUSED, ThemesData *d)
{
    store_selected_theme (d, d->gtk_flow, d->theme_s, KEY_GTK_THEME);
}

static void
on_icon_theme_selected (GtkFlowBox *f G_GNUC_UNUSED,
                        GtkFlowBoxChild *c G_GNUC_UNUSED, ThemesData *d)
{
    store_selected_theme (d, d->icon_flow, d->theme_s, KEY_ICON_THEME);
}

static void
on_cursor_theme_selected (GtkFlowBox *f G_GNUC_UNUSED,
                          GtkFlowBoxChild *c G_GNUC_UNUSED, ThemesData *d)
{
    store_selected_theme (d, d->cursor_flow, d->mouse_s, cursor_key);
}

static void
on_wm_theme_changed (GtkComboBox *cb, ThemesData *d)
{
    if (d->updating || !d->wm_s) return;

    const gchar *name = gtk_combo_box_get_active_id (cb);
    if (name) g_settings_set_string (d->wm_s, KEY_WM_THEME, name);
}

typedef struct { GSettings *settings; const gchar *key; } FontBinding;

static void
on_font_set (GtkFontButton *btn, FontBinding *fb)
{
    g_settings_set_string (fb->settings, fb->key,
                           gtk_font_button_get_font_name (btn));
}

static GtkWidget *
make_font_button (GSettings *settings, const gchar *key)
{
    gchar *font = g_settings_get_string (settings, key);
    GtkWidget *btn = gtk_font_button_new_with_font (font && *font ? font : "Sans 10");
    g_free (font);

    FontBinding *fb = g_new0 (FontBinding, 1);
    fb->settings = settings;
    fb->key      = key;

    g_signal_connect_data (btn, "font-set", G_CALLBACK (on_font_set), fb,
                           cc_closure_free_data, 0);
    return btn;
}

static gboolean
on_title_system_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, ThemesData *d)
{
    g_settings_set_boolean (d->wm_s, KEY_TITLEBAR_SYS, state);
    gtk_widget_set_sensitive (d->title_font_btn, !state);
    return FALSE;
}

typedef struct { ThemesData *d; const gchar *key; } RenderBinding;

static void
on_render_combo_changed (GtkComboBox *cb, RenderBinding *rb)
{
    if (rb->d->updating) return;

    const gchar *value = gtk_combo_box_get_active_id (cb);
    if (value) g_settings_set_string (rb->d->iface_s, rb->key, value);
}

static void
connect_render_combo (ThemesData *d, GtkWidget *combo, const gchar *key)
{
    RenderBinding *rb = g_new0 (RenderBinding, 1);
    rb->d   = d;
    rb->key = key;

    g_signal_connect_data (combo, "changed", G_CALLBACK (on_render_combo_changed),
                           rb, cc_closure_free_data, 0);
}

static void
on_cursor_size_changed (GtkSpinButton *spin, ThemesData *d)
{
    if (d->updating) return;
    g_settings_set_int (d->iface_s, KEY_CURSOR_SIZE,
                        gtk_spin_button_get_value_as_int (spin));
}

/* ================================================================== */
/*  Teardown                                                            */
/* ================================================================== */

static void
themes_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, ThemesData *d)
{
    /* theme_s and mouse_s may alias iface_s when only the GNOME schemas
     * are installed; unref each distinct object exactly once. */
    if (d->theme_s && d->theme_s != d->iface_s) g_object_unref (d->theme_s);
    if (d->mouse_s && d->mouse_s != d->iface_s) g_object_unref (d->mouse_s);
    g_clear_object (&d->iface_s);
    g_clear_object (&d->wm_s);
    g_free (d);
}

/* ================================================================== */
/*  Construction helpers                                                */
/* ================================================================== */

static GtkWidget *
make_flow_scroll (GtkWidget **flow_out, gint height)
{
    GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (sw, -1, height);

    GtkWidget *flow = gtk_flow_box_new ();
    gtk_flow_box_set_selection_mode        (GTK_FLOW_BOX (flow), GTK_SELECTION_SINGLE);
    gtk_flow_box_set_homogeneous           (GTK_FLOW_BOX (flow), TRUE);
    gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 2);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 6);
    gtk_flow_box_set_column_spacing        (GTK_FLOW_BOX (flow), 8);
    gtk_flow_box_set_row_spacing           (GTK_FLOW_BOX (flow), 8);
    gtk_widget_set_margin_start  (flow, 4);
    gtk_widget_set_margin_end    (flow, 4);
    gtk_widget_set_margin_top    (flow, 4);
    gtk_widget_set_margin_bottom (flow, 4);

    gtk_container_add (GTK_CONTAINER (sw), flow);
    if (flow_out) *flow_out = flow;
    return sw;
}

static GtkWidget *
make_id_combo (const gchar *const *ids, const gchar *const *labels)
{
    GtkWidget *combo = gtk_combo_box_text_new ();

    for (int i = 0; ids[i]; i++)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), ids[i], _(labels[i]));

    return combo;
}

/* ================================================================== */
/*  Public                                                             */
/* ================================================================== */

GtkWidget *
cc_themes_panel_new (void)
{
    ThemesData *d = g_new0 (ThemesData, 1);
    const gchar *mouse_schema = NULL;

    d->iface_s = cc_settings_resolve (IFACE_SCHEMAS, NULL);
    d->theme_s = cc_settings_resolve (THEME_SCHEMAS, NULL);
    d->mouse_s = cc_settings_resolve (MOUSE_SCHEMAS, &mouse_schema);
    d->wm_s    = cc_settings_resolve (WM_SCHEMAS,    NULL);

    /* org.mate.peripherals-mouse names the key "cursor-theme" as does the
     * GNOME interface schema, so one constant covers both fallbacks. */
    cursor_key = KEY_CURSOR_THEME;
    CC_LOG ("cursor theme lives in '%s'", mouse_schema ? mouse_schema : "(none)");

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (themes_panel_destroyed), d);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("GTK THEME")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), make_flow_scroll (&d->gtk_flow, 220), FALSE, FALSE, 0);
    g_signal_connect (d->gtk_flow, "child-activated", G_CALLBACK (on_gtk_theme_selected), d);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ICON THEME")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), make_flow_scroll (&d->icon_flow, 220), FALSE, FALSE, 0);
    g_signal_connect (d->icon_flow, "child-activated", G_CALLBACK (on_icon_theme_selected), d);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("CURSOR THEME")), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), make_flow_scroll (&d->cursor_flow, 220), FALSE, FALSE, 0);
    g_signal_connect (d->cursor_flow, "child-activated", G_CALLBACK (on_cursor_theme_selected), d);

    /* Window border + cursor size */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("WINDOW BORDER & CURSOR")),
                        FALSE, FALSE, 0);
    GtkGrid *wg = GTK_GRID (cc_page_add_grid (page));

    d->wm_combo = gtk_combo_box_text_new ();
    cc_grid_attach_row (wg, 0, _("Window border"), d->wm_combo);

    d->cursor_size_spin = gtk_spin_button_new_with_range (8, 96, 2);
    if (d->iface_s) {
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->cursor_size_spin),
                                   g_settings_get_int (d->iface_s, KEY_CURSOR_SIZE));
        g_signal_connect (d->cursor_size_spin, "value-changed",
                          G_CALLBACK (on_cursor_size_changed), d);
    } else {
        gtk_widget_set_sensitive (d->cursor_size_spin, FALSE);
    }
    cc_grid_attach_row (wg, 1, _("Cursor size"), d->cursor_size_spin);

    /* Fonts */
    if (d->theme_s || d->wm_s) {
        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("FONTS")), FALSE, FALSE, 0);
        GtkGrid *fg = GTK_GRID (cc_page_add_grid (page));
        gint row = 0;

        if (d->theme_s) {
            d->ui_font_btn   = make_font_button (d->theme_s, KEY_FONT);
            d->doc_font_btn  = make_font_button (d->theme_s, KEY_DOC_FONT);
            d->mono_font_btn = make_font_button (d->theme_s, KEY_MONO_FONT);
            cc_grid_attach_row (fg, row++, _("Interface font"), d->ui_font_btn);
            cc_grid_attach_row (fg, row++, _("Document font"),  d->doc_font_btn);
            cc_grid_attach_row (fg, row++, _("Monospace font"), d->mono_font_btn);
        }

        if (d->wm_s) {
            gboolean use_system = g_settings_get_boolean (d->wm_s, KEY_TITLEBAR_SYS);

            d->title_font_btn = make_font_button (d->wm_s, KEY_TITLEBAR_FONT);
            gtk_widget_set_sensitive (d->title_font_btn, !use_system);

            d->title_system_sw = gtk_switch_new ();
            gtk_widget_set_halign (d->title_system_sw, GTK_ALIGN_START);
            gtk_switch_set_active (GTK_SWITCH (d->title_system_sw), use_system);
            g_signal_connect (d->title_system_sw, "state-set",
                              G_CALLBACK (on_title_system_toggled), d);

            cc_grid_attach_row (fg, row++, _("Titlebar font"), d->title_font_btn);
            cc_grid_attach_row (fg, row++, _("Use interface font for titlebar"),
                                d->title_system_sw);
        }
    }

    /* Font rendering */
    if (d->iface_s) {
        static const gchar *const aa_ids[]      = { "none", "grayscale", "rgba", NULL };
        static const gchar *const aa_labels[]   = { N_("None"), N_("Greyscale"), N_("Subpixel (LCD)") };
        static const gchar *const hint_ids[]    = { "none", "slight", "medium", "full", NULL };
        static const gchar *const hint_labels[] = { N_("None"), N_("Slight"), N_("Medium"), N_("Full") };
        static const gchar *const rgba_ids[]    = { "rgb", "bgr", "vrgb", "vbgr", "rgba", NULL };
        static const gchar *const rgba_labels[] = { "RGB", "BGR", "VRGB", "VBGR", N_("Auto") };

        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("FONT RENDERING")),
                            FALSE, FALSE, 0);
        GtkGrid *rg = GTK_GRID (cc_page_add_grid (page));

        d->aa_combo   = make_id_combo (aa_ids,   aa_labels);
        d->hint_combo = make_id_combo (hint_ids, hint_labels);
        d->rgba_combo = make_id_combo (rgba_ids, rgba_labels);

        cc_grid_attach_row (rg, 0, _("Antialiasing"),   d->aa_combo);
        cc_grid_attach_row (rg, 1, _("Hinting"),        d->hint_combo);
        cc_grid_attach_row (rg, 2, _("Subpixel order"), d->rgba_combo);

        d->updating = TRUE;
        gchar *aa   = g_settings_get_string (d->iface_s, KEY_ANTIALIASING);
        gchar *hint = g_settings_get_string (d->iface_s, KEY_HINTING);
        gchar *rgba = g_settings_get_string (d->iface_s, KEY_RGBA_ORDER);
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->aa_combo),   aa);
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->hint_combo), hint);
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->rgba_combo), rgba);
        g_free (aa); g_free (hint); g_free (rgba);
        d->updating = FALSE;

        connect_render_combo (d, d->aa_combo,   KEY_ANTIALIASING);
        connect_render_combo (d, d->hint_combo, KEY_HINTING);
        connect_render_combo (d, d->rgba_combo, KEY_RGBA_ORDER);
    }

    gtk_widget_show_all (scroll);

    /* Window-border themes are a short list; no deferred build needed. */
    GList *wm_themes = collect_themes (theme_search_dirs (), is_wm_theme);
    if (wm_themes) {
        d->updating = TRUE;
        for (GList *l = wm_themes; l; l = l->next)
            gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->wm_combo), l->data, l->data);

        if (d->wm_s) {
            gchar *current = g_settings_get_string (d->wm_s, KEY_WM_THEME);
            gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->wm_combo), current);
            g_free (current);
        }
        d->updating = FALSE;

        g_list_free_full (wm_themes, g_free);
        g_signal_connect (d->wm_combo, "changed", G_CALLBACK (on_wm_theme_changed), d);
    }

    queue_populate (d, d->gtk_flow,    THUMB_GTK,
                    collect_themes (theme_search_dirs (), is_gtk3_theme),
                    d->theme_s, KEY_GTK_THEME);
    queue_populate (d, d->icon_flow,   THUMB_ICON,
                    collect_themes (icon_search_dirs (), is_icon_theme),
                    d->theme_s, KEY_ICON_THEME);
    queue_populate (d, d->cursor_flow, THUMB_CURSOR,
                    collect_themes (icon_search_dirs (), is_cursor_theme),
                    d->mouse_s, cursor_key);

    return scroll;
}
