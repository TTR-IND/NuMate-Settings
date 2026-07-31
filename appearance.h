/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * appearance.h — minimal shim for the vendored mate-wp-* backend files
 *
 * The vendored files (mate-wp-item, mate-wp-info, mate-wp-xml) from
 * mate-control-center use only wp_hash, thumb_factory and wp_settings,
 * so only those are defined here. The full
 * mate-appearance-properties infrastructure is not required.
 *
 * Copyright (C) 2026 Josh A. Wheatstone - Torfaen Technology Research IND.
 * Copyright (C) 2025 NuMate desktop contributors
 * Licensed under the GNU General Public License v2 or later.
 */

#ifndef APPEARANCE_H
#define APPEARANCE_H

#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <libmate-desktop/mate-desktop-thumbnail.h>

/* GSettings keys — identical to mate-control-center appearance.h */
#define WP_SCHEMA      "org.mate.background"
#define WP_FILE_KEY    "picture-filename"
#define WP_OPTIONS_KEY "picture-options"
#define WP_SHADING_KEY "color-shading-type"
#define WP_PCOLOR_KEY  "primary-color"
#define WP_SCOLOR_KEY  "secondary-color"

#ifndef BACKGROUND_DATADIR
#define BACKGROUND_DATADIR "/usr/share/mate-background-properties"
#endif

/* mate-wp-xml.c uses WALLPAPER_DATADIR for the per-user cache dir */
#ifndef WALLPAPER_DATADIR
#define WALLPAPER_DATADIR BACKGROUND_DATADIR
#endif

/*
 * wp_hash owns both its keys and its values: keys are strdup'ed copies of
 * item->filename, never the item's own pointer. Aliasing the two makes the
 * key destructor and mate_wp_item_free() free the same buffer twice.
 */
typedef struct {
    GHashTable                  *wp_hash;
    MateDesktopThumbnailFactory *thumb_factory;
    GSettings                   *wp_settings;

    gint thumb_width;
    gint thumb_height;
} AppearanceData;

#endif /* APPEARANCE_H */
