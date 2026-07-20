CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -O3 -march=native -DNDEBUG
CFLAGS += -Wno-unused-parameter
CFLAGS += -Isrc -Iinclude -Iprotocol
CFLAGS += $(shell pkg-config --cflags wayland-client cairo pango pangocairo xkbcommon)
LDLIBS  = $(shell pkg-config --libs wayland-client cairo pango pangocairo xkbcommon) -lrt

WAYLAND_SCANNER ?= wayland-scanner

SRC = src/ulaunch.c src/render.c src/input.c src/filter.c src/theme.c src/mode_dmenu.c src/mode_drun.c

WLR_PROTO = protocol/wlr-layer-shell-unstable-v1-client-protocol.h \
            protocol/wlr-layer-shell-unstable-v1-client-protocol.c

FOREIGN_PROTO = protocol/ext-foreign-toplevel-list-v1-client-protocol.h \
                protocol/ext-foreign-toplevel-list-v1-client-protocol.c

XDG_PROTO = protocol/xdg-shell-client-protocol.h \
            protocol/xdg-shell-client-protocol.c

PROTOCOLS = $(WLR_PROTO) $(FOREIGN_PROTO) $(XDG_PROTO)

all: ulaunch

$(WLR_PROTO): protocol/wlr-layer-shell-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header < $< > protocol/wlr-layer-shell-unstable-v1-client-protocol.h
	$(WAYLAND_SCANNER) private-code < $< > protocol/wlr-layer-shell-unstable-v1-client-protocol.c

$(FOREIGN_PROTO): protocol/ext-foreign-toplevel-list-v1.xml
	$(WAYLAND_SCANNER) client-header < $< > protocol/ext-foreign-toplevel-list-v1-client-protocol.h
	$(WAYLAND_SCANNER) private-code < $< > protocol/ext-foreign-toplevel-list-v1-client-protocol.c

$(XDG_PROTO): /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	$(WAYLAND_SCANNER) client-header < $< > protocol/xdg-shell-client-protocol.h
	$(WAYLAND_SCANNER) private-code < $< > protocol/xdg-shell-client-protocol.c

ulaunch: $(SRC) $(PROTOCOLS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(PROTOCOLS) $(LDLIBS)

clean:
	rm -f ulaunch
	rm -f protocol/wlr-layer-shell-unstable-v1-client-protocol.h protocol/wlr-layer-shell-unstable-v1-client-protocol.c
	rm -f protocol/ext-foreign-toplevel-list-v1-client-protocol.h protocol/ext-foreign-toplevel-list-v1-client-protocol.c
	rm -f protocol/xdg-shell-client-protocol.h protocol/xdg-shell-client-protocol.c

install: ulaunch
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp ulaunch $(DESTDIR)$(PREFIX)/bin/ulaunch
	chmod 755 $(DESTDIR)$(PREFIX)/bin/ulaunch

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ulaunch

.PHONY: all clean install uninstall
