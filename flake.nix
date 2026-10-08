{
  description = "UWM - a minimal BSP tiling Wayland compositor built on wlroots";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      eachSystem = f: nixpkgs.lib.genAttrs systems (system: f system nixpkgs.legacyPackages.${system});

      version =
        "1.2.0"
        + nixpkgs.lib.optionalString (self ? shortRev && self.shortRev != null) "+git.${self.shortRev}";

      # wlroots-0.20.pc references these through Requires.private. The root
      # Makefile runs `pkg-config --cflags wlroots-0.20 ...`, which follows
      # Requires.private, so every one of these .pc files must be findable in
      # the build. libinput is omitted: wlroots already propagates it.
      wlrootsPcDeps = pkgs: with pkgs; [
        wayland # wayland-server.pc, wayland-client.pc
        wayland-protocols # wayland-protocols.pc
        libxkbcommon # xkbcommon.pc
        pixman # pixman-1.pc
        libdrm # libdrm.pc
        libGL # egl.pc, glesv2.pc
        libgbm # gbm.pc
        vulkan-loader # vulkan.pc
        lcms2 # lcms2.pc
        systemdLibs # libudev.pc
        seatd # libseat.pc
        libdisplay-info # libdisplay-info.pc
        libliftoff # libliftoff.pc
        libxcb # xcb.pc, xcb-dri3.pc, xcb-present.pc, xcb-render.pc, xcb-shm.pc, xcb-xfixes.pc, xcb-xinput.pc, xcb-composite.pc
        libxcb-render-util # xcb-renderutil.pc
        libxcb-wm # xcb-ewmh.pc, xcb-icccm.pc, xcb-res.pc
        libxcb-errors # xcb-errors.pc
      ];

      commonMeta = pkgs: pname: description: {
        inherit description;
        homepage = "https://github.com/Abhishek-Krishna-A-M/uwm";
        license = pkgs.lib.licenses.mit;
        mainProgram = pname;
        platforms = pkgs.lib.platforms.linux;
      };

      mkApp = program: {
        type = "app";
        inherit program;
      };

      # Store sources contain only what the build reads. Host build outputs
      # (build/, backup/ binaries), reference docs and the companion tools
      # must neither trigger compositor rebuilds nor leak host-built objects
      # into a store build.
      uwmSrc = pkgs: pkgs.lib.fileset.toSource {
        root = ./.;
        fileset = pkgs.lib.fileset.unions [
          ./Makefile
          ./config.def.h
          ./uwm.desktop
          ./src
          ./include
          ./protocol
        ];
      };

      toolSrc = pkgs: dir: files: pkgs.lib.fileset.toSource {
        root = dir;
        fileset = pkgs.lib.fileset.unions files;
      };

      # Shared packaging for the companion tools. The Makefiles resolve
      # wayland-protocols through pkg-config and honour NATIVE=0, so no path
      # patching or flag surgery is needed: plain `make` just works.
      mkTool = pkgs: { pname, src, extraBuildInputs, installPhase, description }:
        pkgs.stdenv.mkDerivation {
          inherit pname version src;

          nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config pkgs.wayland-scanner ];
          buildInputs = [ pkgs.wayland pkgs.wayland-protocols pkgs.cairo pkgs.pango ] ++ extraBuildInputs;

          enableParallelBuilding = true;

          # NATIVE=0: store binaries must run on any machine of this
          # architecture, not just the build host. Local `make` keeps
          # -march=native by default.
          buildPhase = ''
            runHook preBuild
            make NATIVE=0
            runHook postBuild
          '';

          inherit installPhase;

          meta = commonMeta pkgs pname description;
        };
    in
    {
      packages = eachSystem (system: pkgs: rec {
        uwm = pkgs.stdenv.mkDerivation {
          pname = "uwm";
          inherit version;
          src = uwmSrc pkgs;

          nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config ];
          buildInputs = [ pkgs.wlroots_0_20 ] ++ wlrootsPcDeps pkgs;

          enableParallelBuilding = true;

          # NATIVE=0 keeps store binaries portable; WERROR=0 keeps Nix's
          # FORTIFY=3 diagnostics from tripping the Makefile's -Werror. No
          # CFLAGS/LDFLAGS overrides: the Makefile appends to (rather than
          # replaces) the environment, so stdenv flags flow through untouched.
          buildPhase = ''
            runHook preBuild
            make NATIVE=0 WERROR=0
            runHook postBuild
          '';

          # The root Makefile has no install target; mirror the PKGBUILD layout.
          installPhase = ''
            runHook preInstall
            install -Dm755 build/uwm "$out/bin/uwm"
            install -Dm644 uwm.desktop "$out/share/wayland-sessions/uwm.desktop"
            runHook postInstall
          '';

          meta = commonMeta pkgs "uwm" "A minimal BSP tiling Wayland compositor built on wlroots";
        };

        default = uwm;

        ubar = mkTool pkgs {
          pname = "ubar";
          src = toolSrc pkgs ./tools/ubar [
            ./tools/ubar/Makefile
            ./tools/ubar/src
            ./tools/ubar/include
            ./tools/ubar/protocol
          ];
          extraBuildInputs = [ pkgs.pulseaudio pkgs.pipewire pkgs.systemdLibs ];
          description = "Status bar for the UWM Wayland compositor";
          # ubar's Makefile has no install target; install manually.
          installPhase = ''
            runHook preInstall
            install -Dm755 ubar "$out/bin/ubar"
            runHook postInstall
          '';
        };

        ulaunch = mkTool pkgs {
          pname = "ulaunch";
          src = toolSrc pkgs ./tools/ulaunch [
            ./tools/ulaunch/Makefile
            ./tools/ulaunch/src
            ./tools/ulaunch/include
            ./tools/ulaunch/protocol
          ];
          extraBuildInputs = [ pkgs.libxkbcommon ];
          description = "Application launcher for the UWM Wayland compositor";
          # ulaunch's Makefile has a real install target honouring PREFIX.
          installPhase = ''
            runHook preInstall
            make install PREFIX="$out"
            runHook postInstall
          '';
        };
      });

      apps = eachSystem (system: pkgs:
        let
          pkg = pname: self.packages.${system}.${pname};
        in
        rec {
          uwm = mkApp "${pkg "uwm"}/bin/uwm";
          default = uwm;
          ubar = mkApp "${pkg "ubar"}/bin/ubar";
          ulaunch = mkApp "${pkg "ulaunch"}/bin/ulaunch";
        });

      devShells = eachSystem (system: pkgs: {
        default = pkgs.mkShell {
          name = "uwm-dev-shell";

          # Nix's fortified toolchain (FORTIFY 3) emits -Wformat-truncation /
          # -Wunused-result diagnostics that trip the Makefiles' -Werror; the
          # system glibc on FHS distros never sees them. (`make WERROR=0`
          # also works per-invocation.)
          hardeningDisable = [ "fortify3" ];

          # All compile-time and runtime libraries of the compositor and tools.
          inputsFrom = with self.packages.${system}; [ uwm ubar ulaunch ];

          # gcc comes first so `cc` resolves to gcc; clang stays available
          # through `make CC=clang`.
          nativeBuildInputs = with pkgs; [
            gcc
            clang
            gdb
            bear
            valgrind
            pkg-config
            wayland-scanner
            clang-tools
            git
            nixpkgs-fmt
            mesa
            mesa-demos
            libglvnd
          ];

          # Programs the compositor spawns at runtime (see config.h) and
          # helpers for testing a Wayland session.
          buildInputs = with pkgs; [
            swaybg
            foot
            fuzzel
            grim
            slurp
            wl-clipboard
            lf
            fd
            brightnessctl
            wireplumber
            dbus
            xdg-desktop-portal
            xdg-desktop-portal-wlr
            # Implements org.freedesktop.portal.FileChooser. The -wlr backend
            # only covers Screenshot/ScreenCast, so without this every
            # portal-backed GTK file dialog has no backend to talk to.
            xdg-desktop-portal-gtk
          ];

          shellHook = ''
            export LIBGL_DRIVERS_PATH="${pkgs.mesa.drivers}/lib/dri"
            export GBM_BACKENDS_PATH="${pkgs.mesa.drivers}/lib/gbm"

            export XDG_DATA_DIRS="${pkgs.wayland-protocols}/share:${pkgs.shared-mime-info}/share:$XDG_DATA_DIRS"

            echo
            echo "=========================================="
            echo " UWM Development Shell"
            echo "=========================================="
            echo
            echo "Build:"
            echo "  make"
            echo "  make ASAN=1"
            echo
            echo "Debug:"
            echo "  glxinfo -B"
            echo "  eglinfo"
            echo "  WAYLAND_DEBUG=1 ./uwm"
            echo
          '';
        };
      });

      formatter = eachSystem (system: pkgs: pkgs.nixpkgs-fmt);

      checks = eachSystem (system: pkgs:
        with self.packages.${system}; {
          inherit uwm ubar ulaunch;
        });

      nixosModules.default = { pkgs, ... }: {
        # Register the Wayland session with display managers (greetd, GDM,
        # ...) and put the binaries on PATH.
        services.displayManager.sessionPackages = [
          self.packages.${pkgs.stdenv.hostPlatform.system}.uwm
        ];
        environment.systemPackages = with self.packages.${pkgs.stdenv.hostPlatform.system}; [
          uwm
          ubar
          ulaunch
        ];
        # On Wayland, GTK routes file dialogs through xdg-desktop-portal, so the
        # session needs the daemon plus a FileChooser backend. -wlr only
        # provides Screenshot/ScreenCast; -gtk provides FileChooser.
        # `xdg.portal` (not the older services.xdg-desktop-portal) starts the
        # daemon at its store path, which uwm's autostart cannot do because the
        # package ships no `bin` output.
        xdg.portal = {
          enable = true;
          extraPortals = with pkgs; [
            xdg-desktop-portal-gtk
            xdg-desktop-portal-wlr
          ];
          config = {
            "preferred" = {
              "org.freedesktop.impl.portal.FileChooser" = "gtk";
              "org.freedesktop.impl.portal.Screenshot" = "wlr";
              "org.freedesktop.impl.portal.ScreenCast" = "wlr";
            };
          };
        };
      };
    };
}
