# NuMate Settings

A single, unified settings application for the [NuMATE](https://github.com/TTR-IND/NuMATE) desktop.

**ChromeOS-inspired redesign (2026):** The shell and stylesheet have been updated for a cleaner, more seamless experience matching the visual language of ChromeOS Settings. Key changes:

- Dark surface palette with card-like sections
- Rounded sidebar selection highlights
- Persistent search bar in the main content area
- Improved spacing, typography, and control alignment
- New `cc_section_card()` helper for grouping settings into elevated cards

The architecture (one binary, lazy-loaded panels in a GtkStack, GSettings backends) remains unchanged. Panels can gradually adopt the card helper for full visual consistency.

NuMate Settings replaces the MATE Control Center. The old control center was not really an application — it was a launcher grid that shelled out to a dozen separate capplet binaries (`mate-appearance-properties`, `mate-display-properties`, `mate-keyboard-properties`, `mate-screensaver-preferences`, and the rest), each its own process, each with its own window, its own toolkit conventions, and its own idea of what a settings page should look like. Changing your wallpaper and then your mouse speed meant two programs, two windows, and two visual languages.

This is one binary, one window, one sidebar. Twelve panels live in a `GtkStack` and are built on demand the first time you open them.

## Panels

| Category | Panel | Backend |
|---|---|---|
| Appearance | Background | vendored `mate-wp-*` + `MateBG` |
| Appearance | Themes | GTK/icon/cursor themes, fonts, font rendering |
| Appearance | Screen Lock | `org.mate.screensaver`, XEmbed preview |
| Hardware | Display | libXrandr |
| Hardware | Sound | libmatemixer (PulseAudio / ALSA) |
| Hardware | Keyboard | `org.gnome.desktop.peripherals.keyboard`, a11y |
| Hardware | Mouse | mouse + touchpad peripherals schemas |
| Hardware | Power | `org.mate.power-manager` |
| System | Date & Time | `org.freedesktop.timedate1` |
| System | Users | AccountsService |
| System | Default Apps | `GAppInfo` / MIME defaults |
| System | Network | NetworkManager (libnm) |

Panels degrade rather than abort. A missing GSettings schema desensitises the affected widgets and logs a warning; it does not take the application down. Where MATE and GNOME ship competing schemas for the same setting, a single resolver (`cc_settings_resolve`) walks a preference list and uses the first one installed.

## Status

**Working, and honest about what it is.** This release is an architectural consolidation, not a feature rewrite. The panels are the MATE settings family's functionality — the same GSettings keys, the same backends — lifted out of a dozen separate capplet binaries and rehoused behind one interface. Memory-safety and performance problems found during that move were fixed along the way, but the *features* are still substantially MATE's. Reworking them is the next phase, not this one.

### Known limitation: Power

The Power panel requires **`mate-power-manager`** to be installed before its sleep and suspend timeouts do anything. MATE never implemented power management as a daemon — it is an application plus a helper or two, and the `org.mate.power-manager` schema comes with that package. Without it, `cc_settings_resolve` falls back to the gnome-settings-daemon schema, which carries the button and lid actions but not the sleep timeouts, so those combos render insensitive.

This is a dependency inherited from the architecture, not a bug in this application. See the roadmap below.

## Roadmap

Planned work, in no fixed order. Nothing here is committed to a release.

### Themes
A slightly new theming architecture with additional options, while staying GTK-friendly. The intent is to extend what can be themed rather than to invent a parallel system that fights the toolkit — anything built here should still work with stock GTK themes.

### Mouse and input devices
Broader input device coverage. Trackpads deserve real configuration rather than the handful of toggles currently exposed, and other input devices are not addressed at all. Game controller configuration is under consideration.

### Sound
Sample rate selection, plus EQ and other DSP effects. This is the panel with the most headroom — the current one exposes volume, device and port routing and stops there.

### Power
Fork `mate-power-manager` and the settings daemon to integrate power management properly, and drop the external dependency described above. Power management belongs in a daemon, not in an application that has to be installed separately for a settings panel to function.

## Building

Dependencies (Debian/Ubuntu package names):

```
libgtk-3-dev libmatemixer-dev libmate-desktop-dev libxml2-dev
libxrandr-dev libxcursor-dev libnm-dev libx11-dev
```

```sh
make
make install                 # honours DESTDIR and prefix
make install prefix=/usr/local
```

Dev tree (this checkout, no GitHub release):

```sh
sudo ./install.sh                 # build + install to /usr/local
PREFIX=$HOME/.local ./install.sh  # user prefix
sudo ./install.sh --uninstall
```

Builds clean under `-Wall -Wextra -std=c11` with no warnings.

## Usage

```sh
numate-settings                 # open the window
numate-settings --panel=display # jump straight to a panel
```

Panel ids: `background`, `themes`, `screensaver`, `display`, `sound`, `keyboard`, `mouse`, `power`, `datetime`, `users`, `defaultapps`, `network`.

## Layout

```
numate-settings.c   entry point, CSS loading, --panel handling
cc-window.[ch]      main window: sidebar + GtkStack
cc-panel.[ch]       panel descriptor and the single built-in registry
cc-panels.h         the twelve panel constructors
cc-common.[ch]      shared page/grid/scale/switch primitives, schema resolver
cc-<name>.c         one file per panel
mate-wp-*.[ch]      vendored wallpaper backend (see THIRD-PARTY.md)
appearance.h        shim for the vendored backend
data/style.css      chrome the widget code cannot express
```

Each panel is a self-contained translation unit exposing exactly one function, which returns a `GtkWidget` owning all of its state; that state is released from the widget's `destroy` handler. Adding a panel means one row in `BUILTIN_PANELS[]` and one declaration in `cc-panels.h`.

Shared measurements have exactly one definition. Page margins and grid spacing live in `cc-common.h`, not in each panel and not duplicated in the stylesheet.

## Licence

© 2026 Josh Wheatstone TechnyxLabs

**GNU General Public License, version 2 — see [LICENSE](LICENSE).**

Every file is GPL-2.0-or-later except the vendored wallpaper backend from
mate-control-center (`mate-wp-info`, `mate-wp-item`, `mate-wp-xml`), which is
version-2-**only** with no upgrade clause. That is the most restrictive term in
the tree, so the combined binary is GPL-2.0-only. Each file carries an
`SPDX-License-Identifier` recording its own terms.

Copyright is layered, as it is in any long-lived GPL project:

| Component | Held by |
|---|---|
| Application shell — `cc-window`, `cc-panel`, `cc-panels.h`, `cc-common`, `numate-settings.c` | J. A. Wheatstone, as a raw C translation of the UKUI control-center shell (Tianjin KYLIN, GPL-2.0) |
| Twelve panels | MATE Developers (capplet code), Tianjin KYLIN (layout), with modifications by J. A. Wheatstone |
| `mate-wp-*` | Novell, Inc. / Rodney Dawes, with modifications by J. A. Wheatstone |

Where upstream code has been changed, the change is recorded in that file's
header under `MODIFICATIONS`, so the delta survives even if version control
history is lost. Upstream authorship notices are untouched.

Full attribution is in [THIRD-PARTY.md](THIRD-PARTY.md). Not affiliated with
or endorsed by the MATE, UKUI or GNOME projects.

*Licensing summary provided in good faith; it is not legal advice.*
