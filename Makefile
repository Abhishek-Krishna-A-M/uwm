PKG_CONFIG?=pkg-config

PKGS="wlroots-0.20" wayland-server xkbcommon
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)
CFLAGS+=$(CFLAGS_PKG_CONFIG)
LIBS!=$(PKG_CONFIG) --libs $(PKGS)

all: uwm

output/main.o: src/main.c
	$(CC) -c $< -g -Werror $(CFLAGS) -I. -DWLR_USE_UNSTABLE -o $@
uwm: output/main.o
	$(CC) $^ -g -Werror $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f uwm output/main.o

.PHONY: all clean
