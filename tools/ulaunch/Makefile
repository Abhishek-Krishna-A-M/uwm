CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -O3 -march=native -DNDEBUG
CFLAGS += -Wno-unused-parameter
CFLAGS += -Isrc -Iinclude -Iprotocol
CFLAGS += $(shell pkg-config --cflags wayland-client cairo pango pangocairo xkbcommon)
LDLIBS  = $(shell pkg-config --libs wayland-client cairo pango pangocairo xkbcommon) -lrt

WAYLAND_SCANNER ?= wayland-scanner

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,build/%.o,$(SRC))

PROTO_SRC = protocol/wlr-layer-shell-unstable-v1-client-protocol.c \
            protocol/ext-foreign-toplevel-list-v1-client-protocol.c \
            protocol/xdg-shell-client-protocol.c
PROTO_OBJ = $(patsubst protocol/%.c,build/protocol/%.o,$(PROTO_SRC))

PROTO_HDR = protocol/wlr-layer-shell-unstable-v1-client-protocol.h \
            protocol/ext-foreign-toplevel-list-v1-client-protocol.h \
            protocol/xdg-shell-client-protocol.h

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

build/%.o: src/%.c $(PROTO_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/protocol/%.o: protocol/%.c $(PROTO_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ulaunch: $(OBJ) $(PROTO_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(PROTO_OBJ) $(LDLIBS)

clean:
	rm -rf build ulaunch
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
