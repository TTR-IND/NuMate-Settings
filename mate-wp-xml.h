/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Authors: Rodney Dawes <dobey@ximian.com>
 *
 *  Copyright 2003-2006 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02110-1301, USA.
 */

#ifndef _MATE_WP_XML_H_
#define _MATE_WP_XML_H_

#include "appearance.h"

void mate_wp_xml_load_list (AppearanceData *data);

/*
 * Serialises data->wp_hash to ~/.config/mate/backgrounds.xml.
 * The hash table is read only: ownership stays with the caller, which is
 * the only place it may be destroyed. Upstream destroyed it here as well,
 * which freed every item twice once the panel tore itself down.
 */
void mate_wp_xml_save_list (AppearanceData *data);

#endif
