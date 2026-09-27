/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-themes.c — Interface panel (appearance mode, icons, cursors, fonts)
 *
 * Copyright (C) 2026 Josh A. Wheatstone - Torfaen Technology Research IND.
 *
 * Light / Dark only writes gtk-theme (Fluent-grey-Light / Fluent-grey-Dark).
 * Icon pack, cursor and marco theme are left alone.
 */

#define CC_LOG_DOMAIN "cc-themes"

#include <string.h>
#include <math.h>

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

#define GTK_DARK     "Fluent-grey-Dark"
#define GTK_LIGHT    "Fluent-grey-Light"

static const gchar *const IFACE_SCHEMAS[] = { "org.gnome.desktop.interface", NULL };
static const gchar *const THEME_SCHEMAS[] = { "org.mate.interface",
                                              "org.gnome.desktop.interface", NULL };
static const gchar *const MOUSE_SCHEMAS[] = { "org.mate.peripherals-mouse",
                                              "org.gnome.desktop.interface", NULL };
static const gchar *const WM_SCHEMAS[]    = { "org.mate.Marco.general",
                                              "org.gnome.desktop.wm.preferences", NULL };

static const gchar *cursor_key = KEY_CURSOR_THEME;

typedef struct {
    GSettings *iface_s;
    GSettings *theme_s;
    GSettings *wm_s;
    GSettings *mouse_s;
    GtkWidget *dark_card;
    GtkWidget *light_card;
    GtkWidget *dark_preview;
    GtkWidget *light_preview;
    GtkWidget *icon_combo;
    GtkWidget *cursor_combo;
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

static gboolean
is_icon_theme (const gchar *dir, const gchar *name)
{
    gchar *index = g_build_filename (dir, name, "index.theme", NULL);
    gchar *contents = NULL;
    gboolean ok = FALSE;

    if (g_file_get_contents (index, &contents, NULL, NULL) &&
        strstr (contents, "[Icon Theme]")) {
        gchar *td = g_build_filename (dir, name, NULL);
        GDir *d = g_dir_open (td, 0, NULL);
        const gchar *e;

        g_free (td);
        if (d) {
            while ((e = g_dir_read_name (d))) {
                gchar *sub;

                if (!strcmp (e, "cursors") || !strcmp (e, "index.theme") ||
                    !strcmp (e, "thumbnail.png"))
                    continue;
                sub = g_build_filename (dir, name, e, NULL);
                ok = g_file_test (sub, G_FILE_TEST_IS_DIR);
                g_free (sub);
                if (ok)
                    break;
            }
            g_dir_close (d);
        }
    }
    g_free (contents);
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
    int i;

    for (i = 0; dirs[i]; i++) {
        GDir *dir = g_dir_open (dirs[i], 0, NULL);
        const gchar *entry;

        if (!dir)
            continue;
        while ((entry = g_dir_read_name (dir))) {
            gchar *name;

            if (entry[0] == '.')
                continue;
            if (g_hash_table_contains (seen, entry))
                continue;
            if (!pred (dirs[i], entry))
                continue;
            name = g_strdup (entry);
            g_hash_table_add (seen, name);
            out = g_list_prepend (out, name);
        }
        g_dir_close (dir);
    }
    g_hash_table_destroy (seen);
    return g_list_sort (out, (GCompareFunc) g_strcmp0);
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

static gboolean
theme_is_light (ThemesData *d)
{
    gchar *gtk;
    gboolean light;

    if (!d->theme_s)
        return FALSE;
    gtk = g_settings_get_string (d->theme_s, KEY_GTK_THEME);
    light = (gtk && strstr (gtk, "Light") != NULL);
    g_free (gtk);
    return light;
}

static void
set_str (GSettings *s, const gchar *key, const gchar *value)
{
    if (s && key && value)
        g_settings_set_string (s, key, value);
}

static void
mark_cards (ThemesData *d, gboolean light)
{
    gtk_style_context_remove_class (gtk_widget_get_style_context (d->dark_card), "active");
    gtk_style_context_remove_class (gtk_widget_get_style_context (d->light_card), "active");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (light ? d->light_card : d->dark_card), "active");
    if (d->dark_preview) {
        g_object_set_data (G_OBJECT (d->dark_preview), "selected",
                           GINT_TO_POINTER (!light));
        gtk_widget_queue_draw (d->dark_preview);
    }
    if (d->light_preview) {
        g_object_set_data (G_OBJECT (d->light_preview), "selected",
                           GINT_TO_POINTER (light));
        gtk_widget_queue_draw (d->light_preview);
    }
}

static void
apply_appearance (ThemesData *d, gboolean light)
{
    d->updating = TRUE;
    set_str (d->theme_s, KEY_GTK_THEME, light ? GTK_LIGHT : GTK_DARK);
    mark_cards (d, light);
    d->updating = FALSE;
}

static void
on_appearance_clicked (GtkButton *btn, ThemesData *d)
{
    apply_appearance (d, (GtkWidget *) btn == d->light_card);
}

static void
rounded_rect (cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path (cr);
    cairo_arc (cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc (cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc (cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc (cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path (cr);
}

static gboolean
on_preview_draw (GtkWidget *w, cairo_t *cr, gpointer light_ptr)
{
    gboolean light = GPOINTER_TO_INT (light_ptr);
    gboolean selected = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (w), "selected"));
    int width = gtk_widget_get_allocated_width (w);
    int height = gtk_widget_get_allocated_height (w);
    double pad = selected ? 5.0 : 3.0;
    double rr = 18.0;
    double x = pad, y = pad, pw = width - pad * 2, ph = height - pad * 2;
    cairo_pattern_t *wall;
    double wx, wy, ww, wh;

    if (selected) {
        cairo_set_source_rgb (cr, 0.00, 0.48, 1.00);
        rounded_rect (cr, 1, 1, width - 2, height - 2, 20);
        cairo_set_line_width (cr, 3.5);
        cairo_stroke (cr);
    }

    rounded_rect (cr, x, y, pw, ph, rr);
    cairo_clip (cr);

    if (light)
        wall = cairo_pattern_create_linear (x, y, x + pw, y + ph);
    else
        wall = cairo_pattern_create_linear (x + pw, y, x, y + ph);

    if (light) {
        cairo_pattern_add_color_stop_rgb (wall, 0.00, 0.98, 0.62, 0.28);
        cairo_pattern_add_color_stop_rgb (wall, 0.45, 0.35, 0.62, 0.95);
        cairo_pattern_add_color_stop_rgb (wall, 1.00, 0.55, 0.78, 0.96);
    } else {
        cairo_pattern_add_color_stop_rgb (wall, 0.00, 0.05, 0.18, 0.42);
        cairo_pattern_add_color_stop_rgb (wall, 0.50, 0.10, 0.45, 0.62);
        cairo_pattern_add_color_stop_rgb (wall, 1.00, 0.22, 0.12, 0.48);
    }
    cairo_set_source (cr, wall);
    cairo_paint (cr);
    cairo_pattern_destroy (wall);

    /* menu strip */
    if (light)
        cairo_set_source_rgba (cr, 1, 1, 1, 0.72);
    else
        cairo_set_source_rgba (cr, 0.12, 0.16, 0.28, 0.78);
    rounded_rect (cr, x + pw * 0.10, y + ph * 0.16, pw * 0.55, ph * 0.16, 6);
    cairo_fill (cr);

    /* window */
    ww = pw * 0.62;
    wh = ph * 0.52;
    wx = x + pw * 0.28;
    wy = y + ph * 0.38;
    rounded_rect (cr, wx, wy, ww, wh, 8);
    if (light)
        cairo_set_source_rgb (cr, 0.96, 0.96, 0.97);
    else
        cairo_set_source_rgb (cr, 0.12, 0.12, 0.14);
    cairo_fill (cr);

    /* traffic lights */
    cairo_set_source_rgb (cr, 1.00, 0.37, 0.37);
    cairo_arc (cr, wx + 10, wy + 10, 3.2, 0, 2 * G_PI);
    cairo_fill (cr);
    cairo_set_source_rgb (cr, 1.00, 0.76, 0.18);
    cairo_arc (cr, wx + 20, wy + 10, 3.2, 0, 2 * G_PI);
    cairo_fill (cr);
    cairo_set_source_rgb (cr, 0.20, 0.80, 0.32);
    cairo_arc (cr, wx + 30, wy + 10, 3.2, 0, 2 * G_PI);
    cairo_fill (cr);

    return FALSE;
}

static GtkWidget *
make_appearance_card (ThemesData *d, const gchar *title, gboolean light)
{
    GtkWidget *btn = gtk_button_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *preview = gtk_drawing_area_new ();
    GtkWidget *label = gtk_label_new (title);

    gtk_widget_set_size_request (preview, 176, 118);
    g_signal_connect (preview, "draw", G_CALLBACK (on_preview_draw),
                      GINT_TO_POINTER (light));
    if (light)
        d->light_preview = preview;
    else
        d->dark_preview = preview;

    gtk_button_set_relief (GTK_BUTTON (btn), GTK_RELIEF_NONE);
    gtk_style_context_add_class (gtk_widget_get_style_context (btn), "appearance-card");
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "appearance-caption");
    gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (box), preview, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (btn), box);
    return btn;
}

static GtkWidget *
make_name_combo (GList *names, const gchar *current)
{
    GtkWidget *combo = gtk_combo_box_text_new ();
    GList *l;

    for (l = names; l; l = l->next)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), l->data, l->data);
    if (current)
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), current);
    return combo;
}

static void
on_icon_changed (GtkComboBox *cb, ThemesData *d)
{
    const gchar *id;

    if (d->updating || !d->theme_s)
        return;
    id = gtk_combo_box_get_active_id (cb);
    if (id)
        g_settings_set_string (d->theme_s, KEY_ICON_THEME, id);
}

static void
on_cursor_changed (GtkComboBox *cb, ThemesData *d)
{
    const gchar *id;

    if (d->updating || !d->mouse_s)
        return;
    id = gtk_combo_box_get_active_id (cb);
    if (id)
        g_settings_set_string (d->mouse_s, cursor_key, id);
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
    FontBinding *fb = g_new0 (FontBinding, 1);

    g_free (font);
    fb->settings = settings;
    fb->key = key;
    g_signal_connect_data (btn, "font-set", G_CALLBACK (on_font_set), fb,
                           cc_closure_free_data, 0);
    return btn;
}

static gboolean
on_title_system_toggled (GtkSwitch *sw, gboolean state, ThemesData *d)
{
    (void) sw;
    g_settings_set_boolean (d->wm_s, KEY_TITLEBAR_SYS, state);
    gtk_widget_set_sensitive (d->title_font_btn, !state);
    return FALSE;
}

typedef struct { ThemesData *d; const gchar *key; } RenderBinding;

static void
on_render_combo_changed (GtkComboBox *cb, RenderBinding *rb)
{
    const gchar *value;

    if (rb->d->updating)
        return;
    value = gtk_combo_box_get_active_id (cb);
    if (value)
        g_settings_set_string (rb->d->iface_s, rb->key, value);
}

static void
connect_render_combo (ThemesData *d, GtkWidget *combo, const gchar *key)
{
    RenderBinding *rb = g_new0 (RenderBinding, 1);

    rb->d = d;
    rb->key = key;
    g_signal_connect_data (combo, "changed", G_CALLBACK (on_render_combo_changed),
                           rb, cc_closure_free_data, 0);
}

static void
on_cursor_size_changed (GtkSpinButton *spin, ThemesData *d)
{
    if (d->updating || !d->iface_s)
        return;
    g_settings_set_int (d->iface_s, KEY_CURSOR_SIZE,
                        gtk_spin_button_get_value_as_int (spin));
}

static GtkWidget *
make_id_combo (const gchar *const *ids, const gchar *const *labels)
{
    GtkWidget *combo = gtk_combo_box_text_new ();
    int i;

    for (i = 0; ids[i]; i++)
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo), ids[i], _(labels[i]));
    return combo;
}

static void
themes_panel_destroyed (GtkWidget *w, ThemesData *d)
{
    (void) w;
    if (d->theme_s && d->theme_s != d->iface_s)
        g_object_unref (d->theme_s);
    if (d->mouse_s && d->mouse_s != d->iface_s)
        g_object_unref (d->mouse_s);
    g_clear_object (&d->iface_s);
    g_clear_object (&d->wm_s);
    g_free (d);
}

GtkWidget *
cc_themes_panel_new (void)
{
    ThemesData *d = g_new0 (ThemesData, 1);
    GtkWidget *page, *scroll, *row;
    GList *icons, *cursors;
    gchar *cur_icon, *cur_cursor;

    d->iface_s = cc_settings_resolve (IFACE_SCHEMAS, NULL);
    d->theme_s = cc_settings_resolve (THEME_SCHEMAS, NULL);
    d->mouse_s = cc_settings_resolve (MOUSE_SCHEMAS, NULL);
    d->wm_s    = cc_settings_resolve (WM_SCHEMAS, NULL);
    cursor_key = KEY_CURSOR_THEME;

    scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (themes_panel_destroyed), d);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("APPEARANCE")), FALSE, FALSE, 0);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_halign (row, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom (row, 16);
    d->light_card = make_appearance_card (d, _("Light"), TRUE);
    d->dark_card = make_appearance_card (d, _("Dark"), FALSE);
    g_signal_connect (d->dark_card, "clicked", G_CALLBACK (on_appearance_clicked), d);
    g_signal_connect (d->light_card, "clicked", G_CALLBACK (on_appearance_clicked), d);
    gtk_box_pack_start (GTK_BOX (row), d->light_card, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (row), d->dark_card, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (page), row, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("ICONS & CURSOR")), FALSE, FALSE, 0);
    {
        GtkGrid *g = GTK_GRID (cc_page_add_grid (page));

        cur_icon = d->theme_s ? g_settings_get_string (d->theme_s, KEY_ICON_THEME) : NULL;
        cur_cursor = d->mouse_s ? g_settings_get_string (d->mouse_s, cursor_key) : NULL;
        icons = collect_themes (icon_search_dirs (), is_icon_theme);
        cursors = collect_themes (icon_search_dirs (), is_cursor_theme);
        d->icon_combo = make_name_combo (icons, cur_icon);
        d->cursor_combo = make_name_combo (cursors, cur_cursor);
        cc_grid_attach_row (g, 0, _("Icon theme"), d->icon_combo);
        cc_grid_attach_row (g, 1, _("Cursor theme"), d->cursor_combo);
        g_signal_connect (d->icon_combo, "changed", G_CALLBACK (on_icon_changed), d);
        g_signal_connect (d->cursor_combo, "changed", G_CALLBACK (on_cursor_changed), d);
        d->cursor_size_spin = gtk_spin_button_new_with_range (8, 96, 2);
        if (d->iface_s) {
            gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->cursor_size_spin),
                                       g_settings_get_int (d->iface_s, KEY_CURSOR_SIZE));
            g_signal_connect (d->cursor_size_spin, "value-changed",
                              G_CALLBACK (on_cursor_size_changed), d);
        } else {
            gtk_widget_set_sensitive (d->cursor_size_spin, FALSE);
        }
        cc_grid_attach_row (g, 2, _("Cursor size"), d->cursor_size_spin);
        g_list_free_full (icons, g_free);
        g_list_free_full (cursors, g_free);
        g_free (cur_icon);
        g_free (cur_cursor);
    }

    if (d->theme_s || d->wm_s) {
        GtkGrid *fg = GTK_GRID (cc_page_add_grid (page));
        gint rowi = 0;

        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("FONTS")), FALSE, FALSE, 0);
        if (d->theme_s) {
            d->ui_font_btn = make_font_button (d->theme_s, KEY_FONT);
            cc_grid_attach_row (fg, rowi++, _("Interface"), d->ui_font_btn);
            d->doc_font_btn = make_font_button (d->theme_s, KEY_DOC_FONT);
            cc_grid_attach_row (fg, rowi++, _("Documents"), d->doc_font_btn);
            d->mono_font_btn = make_font_button (d->theme_s, KEY_MONO_FONT);
            cc_grid_attach_row (fg, rowi++, _("Monospace"), d->mono_font_btn);
        }
        if (d->wm_s) {
            gboolean sys = g_settings_get_boolean (d->wm_s, KEY_TITLEBAR_SYS);

            d->title_system_sw = gtk_switch_new ();
            gtk_switch_set_active (GTK_SWITCH (d->title_system_sw), sys);
            g_signal_connect (d->title_system_sw, "state-set",
                              G_CALLBACK (on_title_system_toggled), d);
            cc_grid_attach_row (fg, rowi++, _("Titlebar uses interface font"),
                                d->title_system_sw);
            d->title_font_btn = make_font_button (d->wm_s, KEY_TITLEBAR_FONT);
            gtk_widget_set_sensitive (d->title_font_btn, !sys);
            cc_grid_attach_row (fg, rowi++, _("Titlebar"), d->title_font_btn);
        }
    }

    if (d->iface_s) {
        static const gchar *aa_ids[] = { "none", "grayscale", "rgba", NULL };
        static const gchar *aa_labels[] = { "None", "Grayscale", "Subpixel", NULL };
        static const gchar *hint_ids[] = { "none", "slight", "medium", "full", NULL };
        static const gchar *hint_labels[] = { "None", "Slight", "Medium", "Full", NULL };
        static const gchar *rgba_ids[] = { "rgb", "bgr", "vrgb", "vbgr", NULL };
        static const gchar *rgba_labels[] = { "RGB", "BGR", "VRGB", "VBGR", NULL };
        GtkGrid *rg = GTK_GRID (cc_page_add_grid (page));
        gchar *aa, *hint, *rgba;

        gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("RENDERING")), FALSE, FALSE, 0);
        d->aa_combo = make_id_combo (aa_ids, aa_labels);
        d->hint_combo = make_id_combo (hint_ids, hint_labels);
        d->rgba_combo = make_id_combo (rgba_ids, rgba_labels);
        cc_grid_attach_row (rg, 0, _("Antialiasing"), d->aa_combo);
        cc_grid_attach_row (rg, 1, _("Hinting"), d->hint_combo);
        cc_grid_attach_row (rg, 2, _("Subpixel order"), d->rgba_combo);
        d->updating = TRUE;
        aa = g_settings_get_string (d->iface_s, KEY_ANTIALIASING);
        hint = g_settings_get_string (d->iface_s, KEY_HINTING);
        rgba = g_settings_get_string (d->iface_s, KEY_RGBA_ORDER);
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->aa_combo), aa);
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->hint_combo), hint);
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->rgba_combo), rgba);
        g_free (aa);
        g_free (hint);
        g_free (rgba);
        d->updating = FALSE;
        connect_render_combo (d, d->aa_combo, KEY_ANTIALIASING);
        connect_render_combo (d, d->hint_combo, KEY_HINTING);
        connect_render_combo (d, d->rgba_combo, KEY_RGBA_ORDER);
    }

    mark_cards (d, theme_is_light (d));
    gtk_widget_show_all (scroll);
    return scroll;
}
