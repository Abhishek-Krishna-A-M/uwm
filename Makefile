PKG_CONFIG?=pkg-config

PKGS="wlroots-0.20" wayland-server xkbcommon libinput
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)

LIBS!=$(PKG_CONFIG) --libs $(PKGS)

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,output/%.o,$(SRC))

PROTO_SRC = protocol/wlr-layer-shell-unstable-v1-protocol.c \
            protocol/uwm-bar-unstable-v1-protocol.c
PROTO_OBJ = $(PROTO_SRC:.c=.o)

XDG_PROTO_OBJ = xdg-shell-protocol.o

BASE_FLAGS = -Werror -Iinclude -I. -Iprotocol -DWLR_USE_UNSTABLE

ifdef ASAN
CFLAGS = -g -fsanitize=address -fno-omit-frame-pointer -O0
LDFLAGS = -fsanitize=address
$(info Building with AddressSanitizer)
else
CFLAGS = -O2 -DNDEBUG
LDFLAGS =
endif

all: config.h uwm

config.h: config.def.h
	cp config.def.h config.h

$(OBJ): config.h

output/%.o: src/%.c
	mkdir -p output
	$(CC) -c $< $(CFLAGS_PKG_CONFIG) $(BASE_FLAGS) $(CFLAGS) -o $@

%.o: %.c
	$(CC) -c $< $(CFLAGS_PKG_CONFIG) $(BASE_FLAGS) $(CFLAGS) -o $@

uwm: $(OBJ) $(PROTO_OBJ) $(XDG_PROTO_OBJ)
	$(CC) $(OBJ) $(PROTO_OBJ) $(XDG_PROTO_OBJ) $(CFLAGS_PKG_CONFIG) $(BASE_FLAGS) $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -rf uwm output/ protocol/*.o xdg-shell-protocol.o

distclean: clean
	rm -f config.h

.PHONY: all clean distclean
