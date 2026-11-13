/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-network.c — Network settings panel
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
 * Backend: NetworkManager via libnm
 *   Copyright (C) 2004-2024 Red Hat, Inc.
 *   Licensed under the GNU Lesser General Public License v2.1 or later.
 *   https://networkmanager.dev
 *
 * Layout ported from the UKUI netconnect plugin:
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
 *   - Device and access-point rows are keyed by the object they describe
 *     and updated in place. Upstream destroyed and rebuilt every row on
 *     any NM notify, losing list selection and scroll position.
 * ---------------------------------------------------------------------
 */

#define CC_LOG_DOMAIN "cc-network"

#include <NetworkManager.h>

#include "cc-common.h"
#include "cc-panels.h"

#define PROXY_SCHEMA "org.gnome.system.proxy"
#define HTTP_SCHEMA  "org.gnome.system.proxy.http"
#define HTTPS_SCHEMA "org.gnome.system.proxy.https"
#define FTP_SCHEMA   "org.gnome.system.proxy.ftp"
#define SOCKS_SCHEMA "org.gnome.system.proxy.socks"

typedef struct {
    NMClient *client;

    GtkWidget *wifi_sw;
    GtkWidget *networking_sw;
    GtkWidget *status_label;

    GtkWidget *conn_list;
    GtkWidget *ap_list;
    GtkWidget *ap_section;

    /*
     * NMDevice* -> row widget and NMAccessPoint* -> row widget.
     *
     * NetworkManager emits a coarse notify for any state change, so the
     * previous revision destroyed and rebuilt every row on every signal:
     * O(devices + access points) of widget churn for a single device
     * changing state, which also dropped the user's list selection and
     * scroll position each time. Rows are now keyed by the object they
     * describe, so a change touches only the rows that actually changed.
     */
    GHashTable *device_rows;
    GHashTable *ap_rows;

    GtkWidget *proxy_mode_combo;
    GtkWidget *auto_url_entry;
    GtkWidget *manual_grid;

    GSettings *proxy_s;
    GSettings *http_s;
    GSettings *https_s;
    GSettings *ftp_s;
    GSettings *socks_s;

    gboolean updating;
} NetworkData;

/* ------------------------------------------------------------------ */
/*  Presentation helpers                                                */
/* ------------------------------------------------------------------ */

static const gchar *
device_state_str (NMDeviceState state)
{
    switch (state) {
    case NM_DEVICE_STATE_ACTIVATED:    return _("Connected");
    case NM_DEVICE_STATE_DISCONNECTED: return _("Disconnected");
    case NM_DEVICE_STATE_PREPARE:
    case NM_DEVICE_STATE_CONFIG:
    case NM_DEVICE_STATE_IP_CONFIG:
    case NM_DEVICE_STATE_IP_CHECK:     return _("Connecting\xe2\x80\xa6");
    case NM_DEVICE_STATE_DEACTIVATING: return _("Disconnecting\xe2\x80\xa6");
    case NM_DEVICE_STATE_UNAVAILABLE:  return _("Unavailable");
    case NM_DEVICE_STATE_UNMANAGED:    return _("Unmanaged");
    default:                           return _("Unknown");
    }
}

static const gchar *
device_icon_name (NMDeviceType type)
{
    switch (type) {
    case NM_DEVICE_TYPE_ETHERNET: return "network-wired";
    case NM_DEVICE_TYPE_WIFI:     return "network-wireless";
    case NM_DEVICE_TYPE_MODEM:    return "network-cellular";
    default:                      return "network-workgroup";
    }
}

static const gchar *
ap_security_str (NMAccessPoint *ap)
{
    if (nm_access_point_get_rsn_flags (ap) != NM_802_11_AP_SEC_NONE) return "WPA2";
    if (nm_access_point_get_wpa_flags (ap) != NM_802_11_AP_SEC_NONE) return "WPA";
    return "";
}

static const gchar *
signal_icon_name (guint8 strength)
{
    if (strength > 75) return "network-wireless-signal-excellent";
    if (strength > 50) return "network-wireless-signal-good";
    if (strength > 25) return "network-wireless-signal-ok";
    return "network-wireless-signal-weak";
}

static const gchar *
client_state_str (NMState state)
{
    switch (state) {
    case NM_STATE_CONNECTED_GLOBAL: return _("Connected");
    case NM_STATE_CONNECTED_SITE:   return _("Connected (site)");
    case NM_STATE_CONNECTED_LOCAL:  return _("Connected (local)");
    case NM_STATE_CONNECTING:       return _("Connecting\xe2\x80\xa6");
    case NM_STATE_DISCONNECTING:    return _("Disconnecting\xe2\x80\xa6");
    case NM_STATE_DISCONNECTED:     return _("Disconnected");
    default:                        return _("Unknown");
    }
}

/* ------------------------------------------------------------------ */
/*  Device rows                                                         */
/* ------------------------------------------------------------------ */

/* Rows keep their mutable labels on the row object so that updating one
 * is a pair of gtk_label_set_text calls rather than a rebuild. */
static GtkWidget *
make_device_row (NMDevice *dev)
{
    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

    gtk_widget_set_margin_start  (box, 8);
    gtk_widget_set_margin_end    (box, 8);
    gtk_widget_set_margin_top    (box, 8);
    gtk_widget_set_margin_bottom (box, 8);

    GtkWidget *icon = gtk_image_new_from_icon_name (
        device_icon_name (nm_device_get_device_type (dev)), GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size (GTK_IMAGE (icon), 22);

    GtkWidget *labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand (labels, TRUE);

    GtkWidget *top = gtk_label_new (NULL);
    gtk_widget_set_halign (top, GTK_ALIGN_START);

    GtkWidget *sub = gtk_label_new (NULL);
    gtk_widget_set_halign (sub, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (sub), "dim-label");

    gtk_box_pack_start (GTK_BOX (labels), top, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (labels), sub, FALSE, FALSE, 0);

    GtkWidget *dot = gtk_label_new (NULL);
    gtk_widget_set_valign (dot, GTK_ALIGN_CENTER);

    gtk_box_pack_start (GTK_BOX (box), icon,   FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), labels, TRUE,  TRUE,  0);
    gtk_box_pack_end   (GTK_BOX (box), dot,    FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (row), box);

    g_object_set_data (G_OBJECT (row), "top-label", top);
    g_object_set_data (G_OBJECT (row), "sub-label", sub);
    g_object_set_data (G_OBJECT (row), "state-dot", dot);
    return row;
}

static const gchar *
device_ip4_address (NMDevice *dev)
{
    NMIPConfig *ip4 = nm_device_get_ip4_config (dev);
    if (!ip4) return NULL;

    const GPtrArray *addrs = nm_ip_config_get_addresses (ip4);
    if (!addrs || addrs->len == 0) return NULL;

    return nm_ip_address_get_address (addrs->pdata[0]);
}

static void
update_device_row (GtkWidget *row, NMDevice *dev)
{
    const gchar *iface = nm_device_get_iface (dev);
    NMDeviceState state = nm_device_get_state (dev);
    NMActiveConnection *ac = nm_device_get_active_connection (dev);

    gchar *top_text;
    if (ac) {
        const gchar *id = nm_active_connection_get_id (ac);
        top_text = g_strdup_printf ("%s \xe2\x80\x94 %s", iface, id ? id : "");
    } else {
        top_text = g_strdup (iface);
    }
    gtk_label_set_text (g_object_get_data (G_OBJECT (row), "top-label"), top_text);
    g_free (top_text);

    const gchar *ip = device_ip4_address (dev);
    gchar *sub_text = ip
        ? g_strdup_printf ("%s \xe2\x80\x94 %s", device_state_str (state), ip)
        : g_strdup (device_state_str (state));
    gtk_label_set_text (g_object_get_data (G_OBJECT (row), "sub-label"), sub_text);
    g_free (sub_text);

    gtk_label_set_text (g_object_get_data (G_OBJECT (row), "state-dot"),
                        state == NM_DEVICE_STATE_ACTIVATED ? "\xe2\x97\x8f" : "\xe2\x97\x8b");
}

/* ------------------------------------------------------------------ */
/*  Access point rows                                                   */
/* ------------------------------------------------------------------ */

static GtkWidget *
make_ap_row (void)
{
    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);

    gtk_widget_set_margin_start  (box, 8);
    gtk_widget_set_margin_end    (box, 8);
    gtk_widget_set_margin_top    (box, 6);
    gtk_widget_set_margin_bottom (box, 6);

    GtkWidget *signal = gtk_image_new ();
    gtk_image_set_pixel_size (GTK_IMAGE (signal), 16);

    GtkWidget *ssid = gtk_label_new (NULL);
    gtk_widget_set_halign (ssid, GTK_ALIGN_START);
    gtk_widget_set_hexpand (ssid, TRUE);

    GtkWidget *security = gtk_label_new (NULL);
    gtk_style_context_add_class (gtk_widget_get_style_context (security), "dim-label");

    gtk_box_pack_start (GTK_BOX (box), signal,   FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), ssid,     TRUE,  TRUE,  0);
    gtk_box_pack_end   (GTK_BOX (box), security, FALSE, FALSE, 0);
    gtk_container_add (GTK_CONTAINER (row), box);

    g_object_set_data (G_OBJECT (row), "signal-image",  signal);
    g_object_set_data (G_OBJECT (row), "ssid-label",    ssid);
    g_object_set_data (G_OBJECT (row), "security-label", security);
    return row;
}

static void
update_ap_row (GtkWidget *row, NMAccessPoint *ap, const gchar *ssid, gboolean active)
{
    gtk_image_set_from_icon_name (
        g_object_get_data (G_OBJECT (row), "signal-image"),
        signal_icon_name (nm_access_point_get_strength (ap)), GTK_ICON_SIZE_MENU);

    GtkLabel *ssid_label = g_object_get_data (G_OBJECT (row), "ssid-label");
    if (active) {
        gchar *markup = g_markup_printf_escaped ("<b>%s</b>", ssid);
        gtk_label_set_markup (ssid_label, markup);
        g_free (markup);
    } else {
        gtk_label_set_text (ssid_label, ssid);
    }

    gtk_label_set_text (g_object_get_data (G_OBJECT (row), "security-label"),
                        ap_security_str (ap));
}

/* ------------------------------------------------------------------ */
/*  Incremental refresh                                                 */
/* ------------------------------------------------------------------ */

/* Destroys rows whose keys are absent from `live`, leaving the rest alone. */
static void
prune_rows (GHashTable *rows, GHashTable *live)
{
    GHashTableIter it;
    gpointer key, value;

    g_hash_table_iter_init (&it, rows);
    while (g_hash_table_iter_next (&it, &key, &value)) {
        if (g_hash_table_contains (live, key)) continue;

        gtk_widget_destroy (GTK_WIDGET (value));
        g_hash_table_iter_remove (&it);
    }
}

static void
refresh_access_points (NetworkData *d, NMDeviceWifi *wdev, GHashTable *live)
{
    const GPtrArray *aps = nm_device_wifi_get_access_points (wdev);
    NMAccessPoint *active = nm_device_wifi_get_active_access_point (wdev);

    for (guint i = 0; i < aps->len; i++) {
        NMAccessPoint *ap = aps->pdata[i];

        GBytes *ssid_bytes = nm_access_point_get_ssid (ap);
        if (!ssid_bytes) continue;

        gchar *ssid = nm_utils_ssid_to_utf8 (g_bytes_get_data (ssid_bytes, NULL),
                                             g_bytes_get_size (ssid_bytes));
        if (!ssid || !*ssid) { g_free (ssid); continue; }

        GtkWidget *row = g_hash_table_lookup (d->ap_rows, ap);
        if (!row) {
            row = make_ap_row ();
            gtk_list_box_insert (GTK_LIST_BOX (d->ap_list), row, -1);
            gtk_widget_show_all (row);
            g_hash_table_insert (d->ap_rows, ap, row);
        }

        update_ap_row (row, ap, ssid, ap == active);
        g_hash_table_add (live, ap);
        g_free (ssid);
    }
}

static void
refresh_connections (NetworkData *d)
{
    if (!d->client) return;

    GHashTable *live_devices = g_hash_table_new (g_direct_hash, g_direct_equal);
    GHashTable *live_aps     = g_hash_table_new (g_direct_hash, g_direct_equal);
    gboolean have_wifi = FALSE;

    const GPtrArray *devices = nm_client_get_devices (d->client);
    for (guint i = 0; i < devices->len; i++) {
        NMDevice *dev = devices->pdata[i];

        /* Loopback is never actionable from a settings panel. */
        if (g_strcmp0 (nm_device_get_iface (dev), "lo") == 0) continue;

        GtkWidget *row = g_hash_table_lookup (d->device_rows, dev);
        if (!row) {
            row = make_device_row (dev);
            gtk_list_box_insert (GTK_LIST_BOX (d->conn_list), row, -1);
            gtk_widget_show_all (row);
            g_hash_table_insert (d->device_rows, dev, row);
        }

        update_device_row (row, dev);
        g_hash_table_add (live_devices, dev);

        if (nm_device_get_device_type (dev) == NM_DEVICE_TYPE_WIFI) {
            have_wifi = TRUE;
            refresh_access_points (d, NM_DEVICE_WIFI (dev), live_aps);
        }
    }

    prune_rows (d->device_rows, live_devices);
    prune_rows (d->ap_rows,     live_aps);

    g_hash_table_destroy (live_devices);
    g_hash_table_destroy (live_aps);

    gtk_widget_set_visible (d->ap_section,
                            have_wifi && nm_client_wireless_get_enabled (d->client));

    gtk_label_set_text (GTK_LABEL (d->status_label),
                        client_state_str (nm_client_get_state (d->client)));

    d->updating = TRUE;
    gtk_switch_set_active (GTK_SWITCH (d->networking_sw),
                           nm_client_networking_get_enabled (d->client));
    gtk_switch_set_active (GTK_SWITCH (d->wifi_sw),
                           nm_client_wireless_get_enabled (d->client));
    d->updating = FALSE;
}

static void
on_nm_changed (GObject *source G_GNUC_UNUSED, GParamSpec *pspec G_GNUC_UNUSED,
               NetworkData *d)
{
    refresh_connections (d);
}

static void
on_nm_device_list_changed (NMClient *client G_GNUC_UNUSED, NMDevice *dev G_GNUC_UNUSED,
                           NetworkData *d)
{
    refresh_connections (d);
}

/* ------------------------------------------------------------------ */
/*  Toggles                                                             */
/* ------------------------------------------------------------------ */

static gboolean
on_wifi_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, NetworkData *d)
{
    if (d->updating || !d->client) return FALSE;

    nm_client_wireless_set_enabled (d->client, state);
    return FALSE;
}

static gboolean
on_networking_toggled (GtkSwitch *sw G_GNUC_UNUSED, gboolean state, NetworkData *d)
{
    if (d->updating || !d->client) return FALSE;

    GError *err = NULL;
    nm_client_networking_set_enabled (d->client, state, &err);
    if (err) {
        g_warning ("cc-network: cannot toggle networking: %s", err->message);
        g_clear_error (&err);
    }
    return FALSE;
}

static void
on_edit_connections (GtkButton *btn G_GNUC_UNUSED, gpointer u G_GNUC_UNUSED)
{
    GError *err = NULL;

    if (!g_spawn_command_line_async ("nm-connection-editor", &err)) {
        g_warning ("cc-network: cannot launch nm-connection-editor: %s", err->message);
        g_clear_error (&err);
    }
}

/* ------------------------------------------------------------------ */
/*  Proxy                                                               */
/* ------------------------------------------------------------------ */

static void
update_proxy_sensitivity (NetworkData *d, const gchar *mode)
{
    gtk_widget_set_sensitive (d->auto_url_entry, g_strcmp0 (mode, "auto")   == 0);
    gtk_widget_set_sensitive (d->manual_grid,    g_strcmp0 (mode, "manual") == 0);
}

static void
on_proxy_mode_changed (GtkComboBox *cb, NetworkData *d)
{
    if (d->updating || !d->proxy_s) return;

    const gchar *mode = gtk_combo_box_get_active_id (cb);
    if (!mode) return;

    g_settings_set_string (d->proxy_s, "mode", mode);
    update_proxy_sensitivity (d, mode);
}

static void
add_proxy_row (GtkGrid *grid, gint row, const gchar *caption, GSettings *settings)
{
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *host = gtk_entry_new ();
    GtkWidget *port = gtk_spin_button_new_with_range (0, 65535, 1);

    gtk_widget_set_hexpand (host, TRUE);
    gtk_widget_set_size_request (port, 80, -1);
    gtk_entry_set_placeholder_text (GTK_ENTRY (host), _("hostname or IP"));

    if (settings) {
        g_settings_bind (settings, "host", host, "text",  G_SETTINGS_BIND_DEFAULT);
        g_settings_bind (settings, "port", port, "value", G_SETTINGS_BIND_DEFAULT);
    } else {
        gtk_widget_set_sensitive (hbox, FALSE);
    }

    gtk_box_pack_start (GTK_BOX (hbox), host, TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (hbox), port, FALSE, FALSE, 0);

    cc_grid_attach_row (grid, row, caption, hbox);
}

/* ------------------------------------------------------------------ */
/*  Teardown                                                            */
/* ------------------------------------------------------------------ */

static void
network_panel_destroyed (GtkWidget *w G_GNUC_UNUSED, NetworkData *d)
{
    /* The tables hold borrowed pointers on both sides: NM owns the keys
     * and the list boxes own the row widgets. */
    g_clear_pointer (&d->device_rows, g_hash_table_destroy);
    g_clear_pointer (&d->ap_rows,     g_hash_table_destroy);

    g_clear_object (&d->client);
    g_clear_object (&d->proxy_s);
    g_clear_object (&d->http_s);
    g_clear_object (&d->https_s);
    g_clear_object (&d->ftp_s);
    g_clear_object (&d->socks_s);
    g_free (d);
}

/* ------------------------------------------------------------------ */
/*  Public                                                              */
/* ------------------------------------------------------------------ */

GtkWidget *
cc_network_panel_new (void)
{
    NetworkData *d = g_new0 (NetworkData, 1);

    d->device_rows = g_hash_table_new (g_direct_hash, g_direct_equal);
    d->ap_rows     = g_hash_table_new (g_direct_hash, g_direct_equal);

    if (cc_schema_exists (PROXY_SCHEMA)) d->proxy_s = g_settings_new (PROXY_SCHEMA);
    if (cc_schema_exists (HTTP_SCHEMA))  d->http_s  = g_settings_new (HTTP_SCHEMA);
    if (cc_schema_exists (HTTPS_SCHEMA)) d->https_s = g_settings_new (HTTPS_SCHEMA);
    if (cc_schema_exists (FTP_SCHEMA))   d->ftp_s   = g_settings_new (FTP_SCHEMA);
    if (cc_schema_exists (SOCKS_SCHEMA)) d->socks_s = g_settings_new (SOCKS_SCHEMA);

    GtkWidget *page;
    GtkWidget *scroll = cc_page_new (&page);
    g_signal_connect (scroll, "destroy", G_CALLBACK (network_panel_destroyed), d);

    /* ---- Status + toggles --------------------------------------- */
    GtkGrid *hdr = GTK_GRID (cc_page_add_grid (page));
    gtk_widget_set_margin_bottom (GTK_WIDGET (hdr), 8);

    d->status_label = gtk_label_new (_("Loading\xe2\x80\xa6"));
    gtk_widget_set_halign (d->status_label, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->status_label), "dim-label");
    gtk_grid_attach (hdr, d->status_label, 0, 0, 2, 1);

    d->networking_sw = gtk_switch_new ();
    gtk_widget_set_halign (d->networking_sw, GTK_ALIGN_START);
    g_signal_connect (d->networking_sw, "state-set",
                      G_CALLBACK (on_networking_toggled), d);
    cc_grid_attach_row (hdr, 1, _("Networking"), d->networking_sw);

    d->wifi_sw = gtk_switch_new ();
    gtk_widget_set_halign (d->wifi_sw, GTK_ALIGN_START);
    g_signal_connect (d->wifi_sw, "state-set", G_CALLBACK (on_wifi_toggled), d);
    cc_grid_attach_row (hdr, 2, _("Wi-Fi"), d->wifi_sw);

    /* ---- Connections -------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("CONNECTIONS")),
                        FALSE, FALSE, 0);

    GtkWidget *conn_frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (conn_frame), GTK_SHADOW_IN);
    d->conn_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (d->conn_list), GTK_SELECTION_NONE);
    gtk_container_add (GTK_CONTAINER (conn_frame), d->conn_list);
    gtk_box_pack_start (GTK_BOX (page), conn_frame, FALSE, TRUE, 0);

    GtkWidget *edit_btn = gtk_button_new_with_label (_("Edit connections\xe2\x80\xa6"));
    gtk_widget_set_halign (edit_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_top (edit_btn, 8);
    g_signal_connect (edit_btn, "clicked", G_CALLBACK (on_edit_connections), NULL);
    gtk_box_pack_start (GTK_BOX (page), edit_btn, FALSE, FALSE, 0);

    /* ---- Wi-Fi networks ----------------------------------------- */
    d->ap_section = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (page), d->ap_section, FALSE, TRUE, 0);

    gtk_box_pack_start (GTK_BOX (d->ap_section),
                        cc_section_label (_("AVAILABLE WI-FI NETWORKS")),
                        FALSE, FALSE, 0);

    GtkWidget *ap_frame = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (ap_frame), GTK_SHADOW_IN);
    GtkWidget *ap_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (ap_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (ap_scroll, -1, 200);
    d->ap_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (d->ap_list), GTK_SELECTION_SINGLE);
    gtk_container_add (GTK_CONTAINER (ap_scroll), d->ap_list);
    gtk_container_add (GTK_CONTAINER (ap_frame), ap_scroll);
    gtk_box_pack_start (GTK_BOX (d->ap_section), ap_frame, FALSE, TRUE, 0);

    /* ---- Proxy -------------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (page), cc_section_label (_("PROXY")), FALSE, FALSE, 0);
    GtkGrid *pg = GTK_GRID (cc_page_add_grid (page));

    d->proxy_mode_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->proxy_mode_combo), "none",   _("Disabled"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->proxy_mode_combo), "manual", _("Manual"));
    gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->proxy_mode_combo), "auto",   _("Automatic (PAC)"));
    cc_grid_attach_row (pg, 0, _("Proxy mode"), d->proxy_mode_combo);

    d->auto_url_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (d->auto_url_entry),
                                    "http://example.com/proxy.pac");
    cc_grid_attach_row (pg, 1, _("PAC URL"), d->auto_url_entry);

    d->manual_grid = cc_page_add_grid (page);
    add_proxy_row (GTK_GRID (d->manual_grid), 0, _("HTTP"),  d->http_s);
    add_proxy_row (GTK_GRID (d->manual_grid), 1, _("HTTPS"), d->https_s);
    add_proxy_row (GTK_GRID (d->manual_grid), 2, _("FTP"),   d->ftp_s);
    add_proxy_row (GTK_GRID (d->manual_grid), 3, _("SOCKS"), d->socks_s);

    if (d->proxy_s) {
        g_settings_bind (d->proxy_s, "autoconfig-url",
                         d->auto_url_entry, "text", G_SETTINGS_BIND_DEFAULT);

        gchar *mode = g_settings_get_string (d->proxy_s, "mode");
        d->updating = TRUE;
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (d->proxy_mode_combo), mode);
        d->updating = FALSE;
        update_proxy_sensitivity (d, mode);
        g_free (mode);
    } else {
        gtk_widget_set_sensitive (GTK_WIDGET (pg), FALSE);
        gtk_widget_set_sensitive (d->manual_grid, FALSE);
    }

    g_signal_connect (d->proxy_mode_combo, "changed",
                      G_CALLBACK (on_proxy_mode_changed), d);

    gtk_widget_show_all (scroll);
    gtk_widget_hide (d->ap_section);   /* revealed once NM reports Wi-Fi */

    /* ---- NetworkManager ----------------------------------------- */
    GError *err = NULL;
    d->client = nm_client_new (NULL, &err);

    if (!d->client) {
        g_warning ("cc-network: NetworkManager unavailable: %s", err->message);
        g_clear_error (&err);
        gtk_label_set_text (GTK_LABEL (d->status_label),
                            _("NetworkManager not available"));
        gtk_widget_set_sensitive (d->networking_sw, FALSE);
        gtk_widget_set_sensitive (d->wifi_sw, FALSE);
        return scroll;
    }

    CC_LOG ("NetworkManager %s", nm_client_get_version (d->client));

    g_signal_connect (d->client, "notify::state",
                      G_CALLBACK (on_nm_changed), d);
    g_signal_connect (d->client, "notify::wireless-enabled",
                      G_CALLBACK (on_nm_changed), d);
    g_signal_connect (d->client, "notify::networking-enabled",
                      G_CALLBACK (on_nm_changed), d);
    g_signal_connect (d->client, "device-added",
                      G_CALLBACK (on_nm_device_list_changed), d);
    g_signal_connect (d->client, "device-removed",
                      G_CALLBACK (on_nm_device_list_changed), d);

    refresh_connections (d);
    return scroll;
}
