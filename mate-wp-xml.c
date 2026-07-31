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
 *
 * ---------------------------------------------------------------------
 * MODIFICATIONS (2026), made for J. A. Wheatstone, Torfaen Technology
 * Research IND. Upstream authorship above is unchanged; these are edits
 * to that work, recorded here so the delta survives outside version
 * control:
 *
 *   - mate_wp_xml_load_xml() inserts an independent key copy, for the
 *     same reason.
 *   - mate_wp_xml_save_list() no longer destroys data->wp_hash or frees
 *     the items it walks; ownership stays with the caller. Upstream
 *     freed everything here, after which panel teardown freed it a
 *     second time.
 *   - mate_wp_xml_load_from_dir() unrefs the GFileEnumerator after
 *     closing it.
 * ---------------------------------------------------------------------
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <libxml/parser.h>
#include <glib/gi18n.h>

#include "mate-wp-xml.h"
#include "mate-wp-item.h"

static gboolean mate_wp_xml_get_bool (const xmlNode* parent, const char* prop_name)
{
	gboolean ret_val = FALSE;

	if (parent == NULL || prop_name == NULL)
		return FALSE;

	xmlChar* prop = xmlGetProp ((xmlNode*) parent, (xmlChar*) prop_name);

	if (prop != NULL)
	{
		ret_val = !g_ascii_strcasecmp ((char*) prop, "true") ||
		          !g_ascii_strcasecmp ((char*) prop, "1");
		xmlFree (prop);
	}

	return ret_val;
}

static void mate_wp_xml_set_bool (const xmlNode* parent, const xmlChar* prop_name, gboolean value)
{
	if (parent == NULL || prop_name == NULL)
		return;

	xmlSetProp ((xmlNode*) parent, prop_name, (xmlChar*) (value ? "true" : "false"));
}

static void mate_wp_load_legacy (AppearanceData* data)
{
	/* Legacy of GNOME2: ~/.gnome2/wallpapers.list */
	char* filename = g_build_filename (g_get_home_dir (), ".gnome2", "wallpapers.list", NULL);
	FILE* fp;

	if (!g_file_test (filename, G_FILE_TEST_EXISTS) ||
	    (fp = fopen (filename, "r")) == NULL)
	{
		g_free (filename);
		return;
	}

	char* line = g_malloc (4096);

	while (fgets (line, 4096, fp))
	{
		size_t len = strlen (line);

		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (g_hash_table_lookup (data->wp_hash, line) != NULL)
			continue;

		if (!g_file_test (line, G_FILE_TEST_EXISTS))
			continue;

		mate_wp_item_new (line, data->wp_hash, data->thumb_factory);
	}

	fclose (fp);
	g_free (line);
	g_free (filename);
}

static void mate_wp_xml_load_xml (AppearanceData* data, const char* filename)
{
	xmlDoc* wplist;
	xmlNode* root;
	xmlNode* list;
	xmlNode* wpa;
	xmlChar* nodelang;
#ifdef ENABLE_NLS
	const char* const* syslangs;
	gint i;
#endif /* ENABLE_NLS */
	GdkRGBA color1;
	GdkRGBA color2;

	wplist = xmlParseFile (filename);

	if (!wplist)
	{
		return;
	}

#ifdef ENABLE_NLS
	syslangs = g_get_language_names ();
#endif /* ENABLE_NLS */

	root = xmlDocGetRootElement (wplist);

	for (list = root->children; list != NULL; list = list->next)
	{
		if (strcmp ((char*) list->name, "wallpaper") != 0)
			continue;

		MateWPItem * wp;
		char *pcolor = NULL, *scolor = NULL;
		gboolean have_scale = FALSE, have_shade = FALSE, have_artist = FALSE;

		wp = g_new0 (MateWPItem, 1);

		wp->deleted = mate_wp_xml_get_bool (list, "deleted");

		for (wpa = list->children; wpa != NULL; wpa = wpa->next)
		{
			if (wpa->type != XML_ELEMENT_NODE)
			{
				continue;
			}

			if (!strcmp ((char*) wpa->name, "filename"))
			{
				if (wpa->last != NULL && wpa->last->content != NULL)
				{
					const char* none = "(none)";
					char* content = g_strstrip ((char*) wpa->last->content);

					if (!strcmp (content, none))
					{
						wp->filename = g_strdup (content);
					}
					else if (g_utf8_validate (content, -1, NULL) && g_file_test (content, G_FILE_TEST_EXISTS))
					{
						wp->filename = g_strdup (content);
					}
					else
					{
						wp->filename = g_filename_from_utf8 (content, -1, NULL, NULL, NULL);
					}
				}
				else
				{
					break;
				}
			}
			else if (!strcmp ((char*) wpa->name, "name"))
			{
				if (wpa->last != NULL && wpa->last->content != NULL)
				{
					nodelang = xmlNodeGetLang (wpa->last);

					if (wp->name == NULL && nodelang == NULL)
					{
						wp->name = g_strdup (g_strstrip ((char *) wpa->last->content));
					}
#ifdef ENABLE_NLS
					else if (nodelang != NULL)
					{
						for (i = 0; syslangs[i] != NULL; i++)
						{
							if (!strcmp (syslangs[i], (char *) nodelang))
							{
								g_free (wp->name);
								wp->name = g_strdup (g_strstrip ((char*) wpa->last->content));
								break;
							}
						}
					}
#endif /* ENABLE_NLS */

					xmlFree (nodelang);
				}
				else
				{
					break;
				}
			}
			else if (!strcmp ((char*) wpa->name, "options"))
			{
				if (wpa->last != NULL)
				{
					wp->options = wp_item_string_to_option (g_strstrip ((char *) wpa->last->content));
					have_scale = TRUE;
				}
			}
			else if (!strcmp ((char*) wpa->name, "shade_type"))
			{
				if (wpa->last != NULL)
				{
					wp->shade_type = wp_item_string_to_shading (g_strstrip ((char *) wpa->last->content));
					have_shade = TRUE;
				}
			}
			else if (!strcmp ((char*) wpa->name, "pcolor"))
			{
				if (wpa->last != NULL)
					pcolor = g_strdup (g_strstrip ((char *) wpa->last->content));
			}
			else if (!strcmp ((char*) wpa->name, "scolor"))
			{
				if (wpa->last != NULL)
					scolor = g_strdup (g_strstrip ((char *) wpa->last->content));
			}
			else if (!strcmp ((char*) wpa->name, "artist"))
			{
				if (wpa->last != NULL)
				{
					wp->artist = g_strdup (g_strstrip ((char *) wpa->last->content));
					have_artist = TRUE;
				}
			}
			else if (!strcmp ((char*) wpa->name, "text"))
			{
				/* libxml2 reports inter-element whitespace as a text node. */
			}
			else
			{
				g_warning ("Unknown Tag: %s", wpa->name);
			}
		}

		/* Reject duplicates and entries whose file has gone away. */
		if (wp->filename == NULL ||
		    g_hash_table_lookup (data->wp_hash, wp->filename) != NULL)
		{
			mate_wp_item_free (wp);
			g_free (pcolor);
			g_free (scolor);
			continue;
		}

		if (!have_scale)
			wp->options = g_settings_get_enum (data->wp_settings, WP_OPTIONS_KEY);

		if (!have_shade)
			wp->shade_type = g_settings_get_enum (data->wp_settings, WP_SHADING_KEY);

		if (pcolor == NULL)
			pcolor = g_settings_get_string (data->wp_settings, WP_PCOLOR_KEY);

		if (scolor == NULL)
			scolor = g_settings_get_string (data->wp_settings, WP_SCOLOR_KEY);

		if (!have_artist)
			wp->artist = g_strdup ("(none)");

		gdk_rgba_parse (&color1, pcolor);
		gdk_rgba_parse (&color2, scolor);
		g_free (pcolor);
		g_free (scolor);

		wp->pcolor = gdk_rgba_copy (&color1);
		wp->scolor = gdk_rgba_copy (&color2);

		if (!strcmp (wp->filename, "(none)") ||
		    g_file_test (wp->filename, G_FILE_TEST_EXISTS))
		{
			wp->fileinfo = mate_wp_info_new (wp->filename, data->thumb_factory);

			if (wp->fileinfo == NULL)
			{
				mate_wp_item_free (wp);
				continue;
			}

			if (wp->name == NULL || !strcmp (wp->filename, "(none)"))
			{
				g_free (wp->name);
				wp->name = g_strdup (wp->fileinfo->name);
			}

			mate_wp_item_ensure_mate_bg (wp);
			mate_wp_item_update_description (wp);
			g_hash_table_insert (data->wp_hash, g_strdup (wp->filename), wp);
		}
		else
		{
			mate_wp_item_free (wp);
		}
	}

	xmlFreeDoc (wplist);
}

static void mate_wp_file_changed (GFileMonitor* monitor, GFile* file, GFile* other_file, GFileMonitorEvent event_type, AppearanceData* data)
{
	char* filename;

	switch (event_type)
	{
		case G_FILE_MONITOR_EVENT_CHANGED:
		case G_FILE_MONITOR_EVENT_CREATED:
			filename = g_file_get_path (file);
			mate_wp_xml_load_xml (data, filename);
			g_free (filename);
			break;
		default:
			break;
	}
}

static void mate_wp_xml_add_monitor (GFile* directory, AppearanceData* data)
{
	GError* error = NULL;

	GFileMonitor* monitor = g_file_monitor_directory (directory, G_FILE_MONITOR_NONE, NULL, &error);

	if (error != NULL)
	{
		char* path = g_file_get_parse_name (directory);
		g_warning ("Unable to monitor directory %s: %s", path, error->message);
		g_error_free (error);
		g_free (path);
		return;
	}

	g_signal_connect (monitor, "changed", G_CALLBACK (mate_wp_file_changed), data);
}

static void mate_wp_xml_load_from_dir (const char* path, AppearanceData* data)
{
	GFile* directory;
	GFileEnumerator* enumerator;
	GError* error = NULL;
	GFileInfo* info;

	if (!g_file_test (path, G_FILE_TEST_IS_DIR))
	{
		return;
	}

	directory = g_file_new_for_path (path);
	enumerator = g_file_enumerate_children (
		directory,
		G_FILE_ATTRIBUTE_STANDARD_NAME,
		G_FILE_QUERY_INFO_NONE,
		NULL,
		&error);

	if (error != NULL)
	{
		g_warning ("Unable to check directory %s: %s", path, error->message);
		g_error_free (error);
		g_object_unref (directory);
		return;
	}

	while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)))
	{
		char* fullpath = g_build_filename (path, g_file_info_get_name (info), NULL);

		g_object_unref (info);

		mate_wp_xml_load_xml (data, fullpath);
		g_free (fullpath);
	}

	g_file_enumerator_close (enumerator, NULL, NULL);
	g_object_unref (enumerator);

	mate_wp_xml_add_monitor (directory, data);

	g_object_unref (directory);
}

void mate_wp_xml_load_list (AppearanceData* data)
{
	const char* const* system_data_dirs;
	char* datadir;
	char* wpdbfile;
	gint i;

	wpdbfile = g_build_filename (g_get_user_config_dir (), "mate", "backgrounds.xml", NULL);

	if (!g_file_test (wpdbfile, G_FILE_TEST_EXISTS))
	{
		g_free (wpdbfile);
		wpdbfile = g_build_filename (g_get_user_config_dir (), "mate", "wp-list.xml", NULL);
	}

	if (g_file_test (wpdbfile, G_FILE_TEST_EXISTS))
		mate_wp_xml_load_xml (data, wpdbfile);

	g_free (wpdbfile);

	datadir = g_build_filename (g_get_user_data_dir (), "mate-background-properties", NULL);
	mate_wp_xml_load_from_dir (datadir, data);
	g_free (datadir);

	system_data_dirs = g_get_system_data_dirs ();

	for (i = 0; system_data_dirs[i]; i++)
	{
		datadir = g_build_filename (system_data_dirs[i], "mate-background-properties", NULL);
		mate_wp_xml_load_from_dir (datadir, data);
		g_free (datadir);
	}

	mate_wp_xml_load_from_dir (WALLPAPER_DATADIR, data);

	mate_wp_load_legacy (data);
}

static void mate_wp_list_flatten (const char* key, MateWPItem* item, GSList** list)
{
	if (key != NULL && item != NULL)
	{
		*list = g_slist_prepend (*list, item);
	}
}

void mate_wp_xml_save_list (AppearanceData* data)
{
	xmlDoc* wplist;
	xmlNode* root;
	xmlNode* wallpaper;
	GSList* list = NULL;
	GSList* l;

	g_hash_table_foreach (data->wp_hash, (GHFunc) mate_wp_list_flatten, &list);
	list = g_slist_reverse (list);

	xmlKeepBlanksDefault (0);

	wplist = xmlNewDoc ((xmlChar*) "1.0");
	xmlCreateIntSubset (wplist, (xmlChar*) "wallpapers", NULL, (xmlChar*) "mate-wp-list.dtd");
	root = xmlNewNode (NULL, (xmlChar*) "wallpapers");
	xmlDocSetRootElement (wplist, root);

	for (l = list; l != NULL; l = l->next)
	{
		MateWPItem* wpitem = l->data;
		const char* none = "(none)";
		char* filename;
		const char* scale;
		const char* shade;
		char* pcolor;
		char* scolor;

		if (!strcmp (wpitem->filename, none) ||
		    (g_utf8_validate (wpitem->filename, -1, NULL) &&
		     g_file_test (wpitem->filename, G_FILE_TEST_EXISTS)))
		{
			filename = g_strdup (wpitem->filename);
		}
		else
		{
			filename = g_filename_to_utf8 (wpitem->filename, -1, NULL, NULL, NULL);
		}

		pcolor = gdk_rgba_to_string (wpitem->pcolor);
		scolor = gdk_rgba_to_string (wpitem->scolor);

		scale = wp_item_option_to_string (wpitem->options);
		shade = wp_item_shading_to_string (wpitem->shade_type);

		wallpaper = xmlNewChild (root, NULL, (xmlChar*) "wallpaper", NULL);
		mate_wp_xml_set_bool (wallpaper, (xmlChar*) "deleted", wpitem->deleted);

		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "name", (xmlChar*) wpitem->name);
		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "filename", (xmlChar*) filename);
		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "options", (xmlChar*) scale);
		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "shade_type", (xmlChar*) shade);
		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "pcolor", (xmlChar*) pcolor);
		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "scolor", (xmlChar*) scolor);
		xmlNewTextChild (wallpaper, NULL, (xmlChar*) "artist", (xmlChar*) wpitem->artist);

		g_free (pcolor);
		g_free (scolor);
		g_free (filename);
	}

	g_slist_free (list);

	/* Only write the document if <wallpapers> actually gained children. */
	if (xmlChildElementCount (root) > 0)
	{
		g_autofree gchar *wpdir = g_build_filename (g_get_user_config_dir (), "mate", NULL);

		if (g_mkdir_with_parents (wpdir, 0700) == -1)
		{
			int errsv = errno;
			g_warning ("g_mkdir_with_parents(%s) failed: %s", wpdir, g_strerror (errsv));
		}
		else
		{
			g_autofree gchar *wpfile = g_build_filename (wpdir, "backgrounds.xml", NULL);
			xmlSaveFormatFile (wpfile, wplist, 1);
		}
	}

	xmlFreeDoc (wplist);
}
