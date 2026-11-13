/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-window.c — main settings window
 *
 * Copyright (C) 2026 Josh Wheatstone TechnyxLabs
 * Copyright (C) 2025 NuMate desktop contributors
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
 * Layout: GtkPaned, sidebar GtkListBox on the left, GtkStack on the right.
 * Built-in panels add their widget straight to the stack; external
 * capplets are embedded through XEmbed via GtkSocket.
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for Josh Wheatstone, TechnyxLabs
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - The separate `sockets` and `pids` tables are merged into one
 *     `external` table of ExternalPanel {page, pid}; the two could fall
 *     out of agreement. on_child_exit() now also destroys the stale
 *     stack page.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-window"

#include <gtk/gtkx.h>

#include "cc-common.h"
#include "cc-panel.h"
#include "cc-window.h"

#define SIDEBAR_WIDTH    240
#define SIDEBAR_POSITION 260

/* An embedded external capplet: its stack page and the process drawing
 * into it. One record per panel id, so teardown never has to keep two
 * tables in agreement. */
typedef struct {
    GtkWidget *page;
    GPid       pid;
} ExternalPanel;

struct _CcWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *paned;
    GtkWidget *sidebar_scroll;
    GtkWidget *sidebar;
    GtkWidget *stack;
    GtkWidget *placeholder;

    GPtrArray  *panels;
    GHashTable *external;   /* id -> ExternalPanel* */
};

G_DEFINE_TYPE (CcWindow, cc_window, GTK_TYPE_APPLICATION_WINDOW)

static void
external_panel_free (ExternalPanel *ep)
{
    if (!ep) return;

    if (ep->pid > 0)
        g_spawn_close_pid (ep->pid);
    g_free (ep);
}

/* ------------------------------------------------------------------ */
/*  Sidebar rows                                                        */
/* ------------------------------------------------------------------ */

static GtkIconTheme *
adwaita_theme (void)
{
    static GtkIconTheme *theme = NULL;
    if (!theme) {
        theme = gtk_icon_theme_new ();
        gtk_icon_theme_set_custom_theme (theme, "Adwaita");
    }
    return theme;
}

static void
update_sidebar_icon (GtkWidget *row, const gchar *icon_name)
{
    GtkWidget *image = g_object_get_data (G_OBJECT (row), "sidebar-icon");
    if (!image || !icon_name) return;

    GtkStyleContext *ctx = gtk_widget_get_style_context (row);
    GdkRGBA fg;
    gtk_style_context_get_color (ctx, gtk_style_context_get_state (ctx), &fg);
    /* Force a light foreground so every sidebar icon is white/monochrome
     * against the dark sidebar, regardless of theme accent colors. */
    if (fg.alpha < 0.1 || (fg.red + fg.green + fg.blue) < 1.5) {
        fg.red = fg.green = fg.blue = 0.91;
        fg.alpha = 1.0;
    }

    gboolean was_symbolic = FALSE;
    GtkIconInfo *info = gtk_icon_theme_lookup_icon (
        adwaita_theme (), icon_name, 22, GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
    if (!info) {
        /* Strip -symbolic and try again. */
        g_autofree gchar *plain = g_str_has_suffix (icon_name, "-symbolic")
            ? g_strndup (icon_name, strlen (icon_name) - 9)
            : g_strdup (icon_name);
        info = gtk_icon_theme_lookup_icon (adwaita_theme (), plain, 22, 0);
    }
    GdkPixbuf *pb = NULL;
    if (info) {
        pb = gtk_icon_info_load_symbolic (info, &fg, NULL, NULL, NULL,
                                          &was_symbolic, NULL);
        g_object_unref (info);
    }

    if (pb) {
        gtk_image_set_from_pixbuf (GTK_IMAGE (image), pb);
        g_object_unref (pb);
    }
}

static void
on_row_style_updated (GtkWidget *row, gpointer user_data)
{
    const gchar *icon_name = g_object_get_data (G_OBJECT (row), "icon-name");
    update_sidebar_icon (row, icon_name);
}

static GtkWidget *
make_panel_row (CcPanel *panel)
{
    GtkWidget *row = gtk_list_box_row_new ();
    g_object_set_data_full (G_OBJECT (row), "panel-id", g_strdup (panel->id), g_free);

    const gchar *icon_name = panel->icon_name ? panel->icon_name : "preferences-system-symbolic";
    g_object_set_data_full (G_OBJECT (row), "icon-name", g_strdup (icon_name), g_free);

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start  (box, 14);
    gtk_widget_set_margin_end    (box, 14);
    gtk_widget_set_margin_top    (box, 5);
    gtk_widget_set_margin_bottom (box, 5);

    GtkWidget *icon = gtk_image_new ();
    gtk_widget_set_valign (icon, GTK_ALIGN_CENTER);
    g_object_set_data (G_OBJECT (row), "sidebar-icon", icon);

    GtkWidget *label = gtk_label_new (panel->name);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand (label, TRUE);

    gtk_box_pack_start (GTK_BOX (box), icon,  FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), label, TRUE,  TRUE,  0);
    gtk_container_add (GTK_CONTAINER (row), box);

    g_signal_connect (row, "style-updated", G_CALLBACK (on_row_style_updated), NULL);
    update_sidebar_icon (row, icon_name);

    gtk_widget_show_all (row);
    return row;
}

static GtkWidget *
make_category_header (const gchar *text)
{
    GtkWidget *row = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
    gtk_list_box_row_set_selectable  (GTK_LIST_BOX_ROW (row), FALSE);
    gtk_widget_set_can_focus (row, FALSE);

    GtkWidget *label = gtk_label_new (text);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_widget_set_margin_start  (label, 12);
    gtk_widget_set_margin_end    (label,  8);
    gtk_widget_set_margin_top    (label, 14);
    gtk_widget_set_margin_bottom (label,  3);
    gtk_style_context_add_class (gtk_widget_get_style_context (label), "category-header");

    gtk_container_add (GTK_CONTAINER (row), label);
    gtk_widget_show_all (row);
    return row;
}

/* ------------------------------------------------------------------ */
/*  Panel activation                                                    */
/* ------------------------------------------------------------------ */

static CcPanel *
find_panel (CcWindow *self, const gchar *id)
{
    for (guint i = 0; i < self->panels->len; i++) {
        CcPanel *p = self->panels->pdata[i];
        if (g_strcmp0 (p->id, id) == 0) return p;
    }
    return NULL;
}

static void
show_page (CcWindow *self, const gchar *id, const gchar *panel_name)
{
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), id);

    if (panel_name) {
        g_autofree gchar *title = g_strdup_printf ("NuMate Settings \xe2\x80\x94 %s", panel_name);
        gtk_window_set_title (GTK_WINDOW (self), title);
    } else {
        gtk_window_set_title (GTK_WINDOW (self), "NuMate Settings");
    }
}

static void
on_child_exit (GPid pid, gint status G_GNUC_UNUSED, gpointer user_data)
{
    CcWindow *self = CC_WINDOW (user_data);

    GHashTableIter it;
    gpointer key, value;

    g_hash_table_iter_init (&it, self->external);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        ExternalPanel *ep = value;
        if (ep->pid != pid) continue;

        /* Removing the page drops the socket with it, so a later
         * activation rebuilds both rather than reusing a dead socket. */
        gtk_widget_destroy (ep->page);
        g_hash_table_iter_remove (&it);
        return;
    }

    g_spawn_close_pid (pid);
}

static gboolean
on_plug_removed (GtkSocket *socket G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
    /* FALSE lets GTK destroy the socket along with the departed plug. */
    return FALSE;
}

static void
activate_external_panel (CcWindow *self, CcPanel *panel)
{
    GtkWidget *socket = gtk_socket_new ();
    g_signal_connect (socket, "plug-removed", G_CALLBACK (on_plug_removed), NULL);

    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (page), socket, TRUE, TRUE, 0);
    gtk_stack_add_named (GTK_STACK (self->stack), page, panel->id);
    gtk_widget_show_all (page);

    gtk_widget_realize (socket);

    gchar **exec_argv = NULL;
    GError *err = NULL;

    if (!g_shell_parse_argv (panel->exec, NULL, &exec_argv, &err)) {
        g_warning ("cc-window: bad exec '%s': %s", panel->exec, err->message);
        g_clear_error (&err);
        gtk_widget_destroy (page);
        return;
    }

    guint n = g_strv_length (exec_argv);
    g_autofree gchar *xid = g_strdup_printf ("%lu",
        (gulong) gtk_socket_get_id (GTK_SOCKET (socket)));

    gchar **argv = g_new0 (gchar *, n + 3);
    for (guint i = 0; i < n; i++) argv[i] = exec_argv[i];
    argv[n]     = (gchar *) "--socket-id";
    argv[n + 1] = xid;

    GPid pid;
    gboolean ok = g_spawn_async (NULL, argv, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL, NULL, &pid, &err);
    g_strfreev (exec_argv);
    g_free (argv);

    if (!ok) {
        g_warning ("cc-window: cannot start '%s': %s", panel->exec, err->message);
        g_clear_error (&err);
        gtk_widget_destroy (page);
        return;
    }

    ExternalPanel *ep = g_new0 (ExternalPanel, 1);
    ep->page = page;
    ep->pid  = pid;
    g_hash_table_insert (self->external, g_strdup (panel->id), ep);

    g_child_watch_add (pid, on_child_exit, self);
    show_page (self, panel->id, panel->name);
}

static void
activate_panel (CcWindow *self, CcPanel *panel)
{
    /* Already built: switching is a stack page change, not a rebuild. */
    if (gtk_stack_get_child_by_name (GTK_STACK (self->stack), panel->id)) {
        show_page (self, panel->id, panel->name);
        return;
    }

    if (panel->create) {
        GtkWidget *w = panel->create ();
        if (!w) {
            g_warning ("cc-window: panel '%s' produced no widget", panel->id);
            return;
        }

        gtk_stack_add_named (GTK_STACK (self->stack), w, panel->id);
        gtk_widget_show_all (w);
        show_page (self, panel->id, panel->name);
        return;
    }

    g_return_if_fail (panel->exec != NULL);
    activate_external_panel (self, panel);
}

static void
on_row_activated (GtkListBox *listbox G_GNUC_UNUSED, GtkListBoxRow *row, CcWindow *self)
{
    const gchar *id = g_object_get_data (G_OBJECT (row), "panel-id");
    if (!id) return;

    CcPanel *p = find_panel (self, id);
    if (p) activate_panel (self, p);
}

/* ------------------------------------------------------------------ */
/*  Sidebar population                                                  */
/* ------------------------------------------------------------------ */

static gint
category_rank (const gchar *category)
{
    if (!g_strcmp0 (category, "Appearance")) return 0;
    if (!g_strcmp0 (category, "Hardware"))   return 1;
    if (!g_strcmp0 (category, "System"))     return 2;
    return 3;
}

static gint
panel_sort_func (gconstpointer a, gconstpointer b)
{
    const CcPanel *pa = *(const CcPanel **) a;
    const CcPanel *pb = *(const CcPanel **) b;

    gint rank = category_rank (pa->category) - category_rank (pb->category);
    if (rank) return rank;
    if (pa->order != pb->order) return pa->order - pb->order;
    return g_strcmp0 (pa->name, pb->name);
}

static void
populate_sidebar (CcWindow *self)
{
    g_ptr_array_sort (self->panels, panel_sort_func);

    const gchar *last_category = NULL;

    for (guint i = 0; i < self->panels->len; i++) {
        CcPanel *p = self->panels->pdata[i];

        if (g_strcmp0 (p->category, last_category) != 0) {
            gtk_list_box_insert (GTK_LIST_BOX (self->sidebar),
                                 make_category_header (p->category), -1);
            last_category = p->category;
        }

        gtk_list_box_insert (GTK_LIST_BOX (self->sidebar), make_panel_row (p), -1);
    }
}

/* ------------------------------------------------------------------ */
/*  GObject                                                             */
/* ------------------------------------------------------------------ */

static void
cc_window_dispose (GObject *object)
{
    CcWindow *self = CC_WINDOW (object);

    g_clear_pointer (&self->panels,   g_ptr_array_unref);
    g_clear_pointer (&self->external, g_hash_table_unref);

    G_OBJECT_CLASS (cc_window_parent_class)->dispose (object);
}

static void
cc_window_class_init (CcWindowClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = cc_window_dispose;
}

static void
cc_window_init (CcWindow *self)
{
    self->external = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                            (GDestroyNotify) external_panel_free);

    gtk_window_set_default_size (GTK_WINDOW (self), 1080, 680);
    gtk_window_set_title        (GTK_WINDOW (self), "NuMate Settings");
    gtk_window_set_icon_name    (GTK_WINDOW (self), "preferences-system");

    self->paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position (GTK_PANED (self->paned), SIDEBAR_POSITION);
    g_object_set (self->paned, "wide-handle", FALSE, NULL);
    gtk_widget_set_can_focus (self->paned, FALSE);

    /* The sidebar has a fixed width by design, so button presses on the
     * divider are swallowed rather than starting a drag. */
    gtk_widget_add_events (self->paned, GDK_BUTTON_PRESS_MASK);
    g_signal_connect (self->paned, "button-press-event", G_CALLBACK (gtk_true), NULL);
    gtk_container_add (GTK_CONTAINER (self), self->paned);

    self->sidebar_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->sidebar_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (self->sidebar_scroll, SIDEBAR_WIDTH, -1);

    self->sidebar = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->sidebar), GTK_SELECTION_SINGLE);
    gtk_style_context_add_class (gtk_widget_get_style_context (self->sidebar), "sidebar");
    g_signal_connect (self->sidebar, "row-activated", G_CALLBACK (on_row_activated), self);
    gtk_container_add (GTK_CONTAINER (self->sidebar_scroll), self->sidebar);

    /* Sidebar column: title + search + list */
    GtkWidget *sidebar_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class (gtk_widget_get_style_context (sidebar_box), "sidebar-column");

    GtkWidget *sidebar_title = gtk_label_new ("Settings");
    gtk_style_context_add_class (gtk_widget_get_style_context (sidebar_title), "sidebar-title");
    gtk_widget_set_halign (sidebar_title, GTK_ALIGN_START);
    gtk_widget_set_margin_start (sidebar_title, 16);
    gtk_widget_set_margin_top (sidebar_title, 16);
    gtk_widget_set_margin_bottom (sidebar_title, 8);
    gtk_box_pack_start (GTK_BOX (sidebar_box), sidebar_title, FALSE, FALSE, 0);

    GtkWidget *search = gtk_search_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (search), "Search settings");
    gtk_style_context_add_class (gtk_widget_get_style_context (search), "search-bar");
    gtk_widget_set_margin_start  (search, 12);
    gtk_widget_set_margin_end    (search, 12);
    gtk_widget_set_margin_bottom (search, 8);
    gtk_box_pack_start (GTK_BOX (sidebar_box), search, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (sidebar_box), self->sidebar_scroll, TRUE, TRUE, 0);

    gtk_paned_add1 (GTK_PANED (self->paned), sidebar_box);

    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_type     (GTK_STACK (self->stack),
                                       GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->stack), 150);

    self->placeholder = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign (self->placeholder, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (self->placeholder, GTK_ALIGN_CENTER);

    GtkWidget *ph_icon = gtk_image_new_from_icon_name ("preferences-system",
                                                       GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (ph_icon), 64);
    GtkWidget *ph_label = gtk_label_new ("Select a setting from the list");
    gtk_style_context_add_class (gtk_widget_get_style_context (ph_icon),  "dim-label");
    gtk_style_context_add_class (gtk_widget_get_style_context (ph_label), "dim-label");

    gtk_box_pack_start (GTK_BOX (self->placeholder), ph_icon,  FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self->placeholder), ph_label, FALSE, FALSE, 0);
    gtk_stack_add_named (GTK_STACK (self->stack), self->placeholder, "placeholder");

    gtk_paned_add2 (GTK_PANED (self->paned), self->stack);

    self->panels = cc_panels_discover ();
    populate_sidebar (self);

    gtk_widget_show_all (GTK_WIDGET (self));
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

GtkWidget *
cc_window_new (GtkApplication *app)
{
    return g_object_new (CC_TYPE_WINDOW, "application", app, NULL);
}

void
cc_window_show_panel (CcWindow *self, const gchar *panel_id)
{
    g_return_if_fail (CC_IS_WINDOW (self));

    CcPanel *p = find_panel (self, panel_id);
    if (!p) {
        g_warning ("cc-window: unknown panel '%s'", panel_id);
        return;
    }

    GList *children = gtk_container_get_children (GTK_CONTAINER (self->sidebar));
    for (GList *l = children; l; l = l->next) {
        if (g_strcmp0 (g_object_get_data (G_OBJECT (l->data), "panel-id"), panel_id) != 0)
            continue;

        gtk_list_box_select_row (GTK_LIST_BOX (self->sidebar), GTK_LIST_BOX_ROW (l->data));
        break;
    }
    g_list_free (children);

    activate_panel (self, p);
}
