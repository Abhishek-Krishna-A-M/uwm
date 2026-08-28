CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -O3 -march=native -DNDEBUG
CFLAGS += -Wno-unused-parameter
CFLAGS += -Iinclude -Isrc -Iprotocol
CFLAGS += $(shell pkg-config --cflags wayland-client cairo pango pangocairo libpulse libpipewire-0.3 libudev)
LDLIBS = $(shell pkg-config --libs wayland-client cairo pango pangocairo libpulse libpipewire-0.3 libudev) -lrt

WAYLAND_SCANNER ?= wayland-scanner

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,build/%.o,$(SRC))

PROTO_SRC = protocol/uwm-bar-unstable-v1-client-protocol.c \
            protocol/wlr-layer-shell-unstable-v1-client-protocol.c \
            protocol/xdg-shell-client-protocol.c
PROTO_OBJ = $(patsubst protocol/%.c,build/protocol/%.o,$(PROTO_SRC))

PROTO_HDR = protocol/uwm-bar-unstable-v1-client-protocol.h \
            protocol/wlr-layer-shell-unstable-v1-client-protocol.h \
            protocol/xdg-shell-client-protocol.h

UWM_PROTO = protocol/uwm-bar-unstable-v1-client-protocol.h \
            protocol/uwm-bar-unstable-v1-client-protocol.c
WLR_PROTO = protocol/wlr-layer-shell-unstable-v1-client-protocol.h \
            protocol/wlr-layer-shell-unstable-v1-client-protocol.c
XDG_PROTO = protocol/xdg-shell-client-protocol.h \
            protocol/xdg-shell-client-protocol.c

PROTOCOLS = $(UWM_PROTO) $(WLR_PROTO) $(XDG_PROTO)

all: ubar

$(UWM_PROTO): protocol/uwm-bar-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header < $< > protocol/uwm-bar-unstable-v1-client-protocol.h
	$(WAYLAND_SCANNER) private-code < $< > protocol/uwm-bar-unstable-v1-client-protocol.c

$(WLR_PROTO): protocol/wlr-layer-shell-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header < $< > protocol/wlr-layer-shell-unstable-v1-client-protocol.h
	$(WAYLAND_SCANNER) private-code < $< > protocol/wlr-layer-shell-unstable-v1-client-protocol.c

$(XDG_PROTO): /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	$(WAYLAND_SCANNER) client-header < $< > protocol/xdg-shell-client-protocol.h
	$(WAYLAND_SCANNER) private-code < $< > protocol/xdg-shell-client-protocol.c

build/%.o: src/%.c $(PROTO_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/protocol/%.o: protocol/%.c $(PROTO_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ubar: $(OBJ) $(PROTO_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(PROTO_OBJ) $(LDLIBS)

clean:
	rm -rf build ubar
	rm -f protocol/uwm-bar-unstable-v1-client-protocol.h protocol/uwm-bar-unstable-v1-client-protocol.c
	rm -f protocol/wlr-layer-shell-unstable-v1-client-protocol.h protocol/wlr-layer-shell-unstable-v1-client-protocol.c
	rm -f protocol/xdg-shell-client-protocol.h protocol/xdg-shell-client-protocol.c

.PHONY: all clean
