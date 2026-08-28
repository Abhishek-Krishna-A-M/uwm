PKG_CONFIG?=pkg-config

PKGS="wlroots-0.20" wayland-server xkbcommon libinput
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)

LIBS!=$(PKG_CONFIG) --libs $(PKGS)

SRC = $(shell find src -name '*.c')
OBJ = $(patsubst src/%.c,build/%.o,$(SRC))

PROTO_SRC = protocol/wlr-layer-shell-unstable-v1-protocol.c \
            protocol/uwm-bar-unstable-v1-protocol.c \
            protocol/xdg-shell-protocol.c
PROTO_OBJ = $(patsubst protocol/%.c,build/protocol/%.o,$(PROTO_SRC))

BASE_FLAGS = -Werror -Iinclude -Iinclude/core -Iinclude/input -Iinclude/output \
             -Iinclude/shell -Iinclude/ui -Iinclude/wm -I. -Iprotocol -DWLR_USE_UNSTABLE

ifdef ASAN
CFLAGS = -g -fsanitize=address -fno-omit-frame-pointer -O0
LDFLAGS = -fsanitize=address
$(info Building with AddressSanitizer)
else
CFLAGS = -O3 -DNDEBUG -march=native -flto
LDFLAGS = -flto
endif

BIN = build/uwm

all: config.h $(BIN)

config.h: config.def.h
	cp config.def.h config.h

$(OBJ): config.h
$(PROTO_OBJ): config.h

build/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) -c $< $(CFLAGS_PKG_CONFIG) $(BASE_FLAGS) $(CFLAGS) -o $@

build/protocol/%.o: protocol/%.c
	mkdir -p $(dir $@)
	$(CC) -c $< $(CFLAGS_PKG_CONFIG) $(BASE_FLAGS) $(CFLAGS) -o $@

$(BIN): $(OBJ) $(PROTO_OBJ)
	mkdir -p $(dir $@)
	$(CC) $(OBJ) $(PROTO_OBJ) $(CFLAGS_PKG_CONFIG) $(BASE_FLAGS) $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@
	ln -sf build/uwm uwm

clean:
	rm -rf build/ output/ uwm protocol/*.o xdg-shell-protocol.o

distclean: clean
	rm -f config.h

.PHONY: all clean distclean
