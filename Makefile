# NuMate Settings — build
#
# Copyright (C) 2026 Josh A. Wheatstone - Torfaen Technology Research IND.
# SPDX-License-Identifier: AGPL-3.0-or-later OR GPL-2.0-or-later
#
# The binary this builds is distributed under GPL-2.0-only, because it
# links GPL-2.0-only code vendored from mate-control-center. See
# THIRD-PARTY.md.

PACKAGE         := numate-settings
VERSION         := 1.0.0
GETTEXT_PACKAGE := $(PACKAGE)

prefix  ?= /usr
datadir ?= $(prefix)/share
bindir  ?= $(prefix)/bin

PKGS := gtk+-3.0 glib-2.0 gio-2.0 gio-unix-2.0 libmatemixer \
        mate-desktop-2.0 libxml-2.0 xrandr xcursor libnm x11

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs   $(PKGS))

# PACKAGE/VERSION/GETTEXT_PACKAGE were #defines at the top of the original
# single-file build. They belong to the build system, not the source.
DEFINES := -DPACKAGE='"$(PACKAGE)"' \
           -DVERSION='"$(VERSION)"' \
           -DGETTEXT_PACKAGE='"$(GETTEXT_PACKAGE)"' \
           -DLOCALEDIR='"$(datadir)/locale"' \
           -DBACKGROUND_DATADIR='"$(datadir)/mate-background-properties"' \
           -DMATE_DESKTOP_USE_UNSTABLE_API \
           -DWNCK_I_KNOW_THIS_IS_UNSTABLE

WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations

CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 $(WARNINGS) $(DEFINES) $(PKG_CFLAGS)
LDLIBS  += $(PKG_LIBS) -lm

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

all: $(PACKAGE)

$(PACKAGE): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Ships the licence and the attribution file, both of which exist in this
# repository. An earlier version referenced LICENSE.GPL-2.0 and
# LICENSE.AGPL-3.0, which do not — so `make install` aborted here even
# though the compile had succeeded, and the caller reported it as a build
# failure. Any file named in this target must exist in the tree.
install: $(PACKAGE)
	install -Dm755 $(PACKAGE) $(DESTDIR)$(bindir)/$(PACKAGE)
	install -Dm644 data/style.css $(DESTDIR)$(datadir)/$(PACKAGE)/style.css
	install -Dm644 data/$(PACKAGE).desktop \
	  $(DESTDIR)$(datadir)/applications/$(PACKAGE).desktop
	install -Dm644 LICENSE \
	  $(DESTDIR)$(datadir)/doc/$(PACKAGE)/LICENSE
	install -Dm644 THIRD-PARTY.md \
	  $(DESTDIR)$(datadir)/doc/$(PACKAGE)/THIRD-PARTY.md

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(PACKAGE)
	$(RM) $(DESTDIR)$(datadir)/$(PACKAGE)/style.css
	$(RM) $(DESTDIR)$(datadir)/applications/$(PACKAGE).desktop
	$(RM) $(DESTDIR)$(datadir)/doc/$(PACKAGE)/LICENSE
	$(RM) $(DESTDIR)$(datadir)/doc/$(PACKAGE)/THIRD-PARTY.md
	-rmdir $(DESTDIR)$(datadir)/doc/$(PACKAGE)
	-rmdir $(DESTDIR)$(datadir)/$(PACKAGE)

clean:
	$(RM) $(OBJS) $(DEPS) $(PACKAGE)

-include $(DEPS)

.PHONY: all install uninstall clean
