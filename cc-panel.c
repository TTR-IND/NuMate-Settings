/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cc-panel.c — settings panel registry
 *
 * Copyright (C) 2026 Josh A. Wheatstone - Torfaen Technology Research IND.
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
 */

#define CC_LOG_DOMAIN "cc-panel"

#include "cc-common.h"
#include "cc-panel.h"
#include "cc-panels.h"

/* The single registry of built-in panels. Adding a panel means adding one
 * row here and one declaration in cc-panels.h; nothing else changes. */
static const struct {
    const gchar *id, *name, *description, *icon, *exec, *category;
    gint order;
    CcPanelCreateFunc create;
} BUILTIN_PANELS[] = {
    { "background",  "Background",   "Desktop wallpaper",
      "preferences-desktop-wallpaper",            NULL, "Appearance", 10, cc_background_panel_new },
    { "themes",      "Themes",       "GTK, icons, cursors, fonts and rendering",
      "preferences-desktop-theme",                NULL, "Appearance", 15, cc_themes_panel_new },
    { "screensaver", "Screen Lock",  "Screen lock and idle timeout",
      "preferences-desktop-screensaver",          NULL, "Appearance", 20, cc_screensaver_panel_new },

    { "display",     "Display",      "Resolution, refresh rate and scale",
      "preferences-desktop-display",              NULL, "Hardware",   10, cc_display_panel_new },
    { "sound",       "Sound",        "Volume, devices and effects",
      "audio-card",                               NULL, "Hardware",   20, cc_sound_panel_new },
    { "keyboard",    "Keyboard",     "Repeat rate and accessibility",
      "preferences-desktop-keyboard",             NULL, "Hardware",   30, cc_keyboard_panel_new },
    { "mouse",       "Mouse",        "Pointer speed, buttons and touchpad",
      "input-mouse",                              NULL, "Hardware",   40, cc_mouse_panel_new },
    { "power",       "Power",        "Sleep, lid and button behaviour",
      "gnome-power-manager",                      NULL, "Hardware",   50, cc_power_panel_new },

    { "datetime",    "Date & Time",  "Clock, timezone and NTP",
      "preferences-system-time",                  NULL, "System",     10, cc_datetime_panel_new },
    { "users",       "Users",        "Account name and password",
      "system-users",                             NULL, "System",     20, cc_users_panel_new },
    { "defaultapps", "Default Apps", "Browser, mail client and media apps",
      "preferences-desktop-default-applications", NULL, "System",     30, cc_defaultapps_panel_new },
    { "network",     "Network",      "Connections, Wi-Fi and proxy",
      "preferences-system-network",               NULL, "System",     40, cc_network_panel_new },
};

void
cc_panel_free (CcPanel *panel)
{
    if (!panel) return;

    g_free (panel->id);
    g_free (panel->name);
    g_free (panel->description);
    g_free (panel->icon_name);
    g_free (panel->exec);
    g_free (panel->category);
    g_free (panel);
}

GPtrArray *
cc_panels_discover (void)
{
    GPtrArray *panels = g_ptr_array_new_with_free_func ((GDestroyNotify) cc_panel_free);

    for (gsize i = 0; i < G_N_ELEMENTS (BUILTIN_PANELS); i++) {
        CcPanel *p = g_new0 (CcPanel, 1);

        p->id          = g_strdup (BUILTIN_PANELS[i].id);
        p->name        = g_strdup (BUILTIN_PANELS[i].name);
        p->description = g_strdup (BUILTIN_PANELS[i].description);
        p->icon_name   = g_strdup (BUILTIN_PANELS[i].icon);
        p->exec        = g_strdup (BUILTIN_PANELS[i].exec);
        p->category    = g_strdup (BUILTIN_PANELS[i].category);
        p->order       = BUILTIN_PANELS[i].order;
        p->create      = BUILTIN_PANELS[i].create;

        g_ptr_array_add (panels, p);
    }

    return panels;
}
