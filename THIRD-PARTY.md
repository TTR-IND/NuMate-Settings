# Third-Party Code and Attribution

© 2026 Josh A. Wheatstone — Torfaen Technology Research IND.

NuMate Settings incorporates code derived from several free software projects. Every source file retains its original copyright and licence header; nothing has been stripped. This document collects those notices in one place.

## Effective licence of the combined work

**GPL-2.0-only.**

Everything else in the tree is GPL-2.0-or-later, but the vendored wallpaper backend is version-2-only, and that governs the combination.

The table below shows why. Most files are GPL-2.0-or-later, but the vendored wallpaper backend from mate-control-center is GPL-2.0-**only** — its headers read "version 2 of the GNU General Public License" with no "or any later version" clause. When code under version-2-only is combined with code under version-2-or-later, the result can only be distributed under version 2. Removing those six files (and the Background panel that depends on them) would relax the aggregate to GPL-2.0-or-later.

## Application shell

`cc-window`, `cc-panel`, `cc-panels.h`, `cc-common` and `numate-settings.c` are a raw C translation of the ukui-control-center application shell, written by J. A. Wheatstone. The original is C++/Qt.

| Origin | Copyright | Licence |
|---|---|---|
| ukui-control-center | © 2019 Tianjin KYLIN Information Technology Co., Ltd. | GPL-2.0 |

A translation is a derivative work, so the UKUI copyright stands alongside the translator's.

## Vendored source

Code copied into this repository, modified, and compiled into the binary.

| Files | Origin | Copyright | Licence |
|---|---|---|---|
| `mate-wp-info.c` `.h` | mate-control-center | © 2003–2006 Novell, Inc. — Author: Rodney Dawes `<dobey@ximian.com>` | GPL-2.0-only |
| `mate-wp-item.c` `.h` | mate-control-center | © 2003–2006 Novell, Inc. — Author: Rodney Dawes | GPL-2.0-only |
| `mate-wp-xml.c` `.h` | mate-control-center | © 2003–2006 Novell, Inc. — Author: Rodney Dawes | GPL-2.0-only |

Upstream: <https://github.com/mate-desktop/mate-control-center>

### Recording of modifications

Every file whose upstream content has been changed carries a `MODIFICATIONS` block in its header listing what was altered and why. Those blocks name J. A. Wheatstone as the party the changes were made for, and leave upstream authorship notices intact. They are deliberately in-source rather than only in git, so the record survives tarball redistribution.

### Modifications to vendored files

These files are not verbatim copies. Changes made here, all documented in-place:

- `mate_wp_item_new()` and `mate_wp_xml_load_xml()` now insert an independent `g_strdup` of `item->filename` as the hash key. Upstream aliased the key to the item's own buffer while the value destructor also freed it, so destroying the table freed the same pointer twice.
- `mate_wp_xml_save_list()` no longer destroys `data->wp_hash` or frees the items it walks. Ownership stays with the caller, which destroys the table exactly once. Upstream's save path freed everything, after which the panel's teardown freed it again.
- `mate_wp_xml_load_from_dir()` unrefs the `GFileEnumerator` after closing it.
- `appearance.h` is a purpose-written shim exposing only the three fields the vendored files use, rather than the full mate-appearance-properties header.

## Derived design and layout

Code written for this project whose structure, GSettings key usage or UI layout follows an existing implementation.

| Component | Derived from | Copyright | Licence |
|---|---|---|---|
| `cc-themes.c` — theme/icon/cursor discovery, font rendering controls | mate-control-center `appearance-themes.c`, `appearance-style.c` — Thomas Wood, Jens Granseuer | © 2007, 2010 The GNOME Foundation | GPL-2.0 |
| `cc-screensaver.c` — saver discovery and XEmbed preview | mate-screensaver `mate-screensaver-preferences.c`, `gs-job.c` — William Jon McCann | © 2004–2006 William Jon McCann; © 2012–2021 MATE Developers | GPL-2.0 |
| Panel layouts across `cc-*.c` | ukui-control-center plugins (wallpaper, theme, display, mouse, keyboard, power, netconnect, datetime, userinfo, defaultapp) | © 2019 Tianjin KYLIN Information Technology Co., Ltd. | GPL-2.0 |
| `cc-themes.c` — cursor thumbnail rasterisation | libXcursor API usage | © 2002 Keith Packard | MIT/X11 |

Upstream: <https://github.com/mate-desktop/mate-screensaver> · <https://github.com/ukui/ukui-control-center>

## Linked libraries

Dynamically linked at build time. Not redistributed here; listed for attribution and licence-compatibility.

| Library | Copyright | Licence |
|---|---|---|
| libmatemixer | © 2014 Michal Ratajsky `<michal.ratajsky@gmail.com>`; © 2014–2021 MATE Developers | LGPL-2.0-or-later |
| libmate-desktop (`MateBG`, `MateDesktopThumbnailFactory`) | © 2000–2001 Ximian, Inc.; © 2014–2021 MATE Developers | LGPL-2.0-or-later |
| NetworkManager / libnm | © 2004–2024 Red Hat, Inc. | LGPL-2.1-or-later |
| GTK 3, GLib, GIO | © The GNOME Project | LGPL-2.1-or-later |
| libxml2 | © Daniel Veillard | MIT |
| libXrandr, libXcursor, libX11 | © X.Org Foundation; © 2002 Keith Packard | MIT/X11 |

All are GPL-2.0 compatible.

## System services

Accessed over D-Bus at runtime; no code incorporated.

- `org.freedesktop.timedate1` (systemd-timedated) — Date & Time
- `org.freedesktop.Accounts` (AccountsService) — Users

## Trademarks and affiliation

MATE is a trademark of the MATE Desktop project. UKUI and Kylin are trademarks of Tianjin KYLIN Information Technology Co., Ltd. GNOME is a trademark of the GNOME Foundation. NuMate Settings is an independent project and is not affiliated with, sponsored by, or endorsed by any of them.

---

*This document is provided in good faith as a record of attribution. It is not legal advice. If you intend to redistribute this software, review the licence headers in the source files themselves — they are authoritative.*
