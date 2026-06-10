PKG_CONFIG?=pkg-config

PKGS="wlroots-0.20" wayland-server xkbcommon pangocairo libinput
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)

# Append dynamic pkg-config flags and your static project flags
CFLAGS+=$(CFLAGS_PKG_CONFIG)
CFLAGS+=-g -Werror -I. -Iinclude -DWLR_USE_UNSTABLE

LIBS!=$(PKG_CONFIG) --libs $(PKGS)

# Dynamically find all .c files in src/ and map them to .o files in output/
SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,output/%.o,$(SRC))


all: uwm

# Pattern rule for all object files
output/%.o: src/%.c
	mkdir -p output
	$(CC) -c $< $(CFLAGS) -o $@

# Link the final executable
uwm: $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

# Clean the executable and the entire output directory
clean:
	rm -rf uwm output/

.PHONY: all clean
