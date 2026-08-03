{
  description = "UWM - a minimal BSP tiling Wayland compositor built on wlroots";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      eachSystem = f: nixpkgs.lib.genAttrs systems (system: f system nixpkgs.legacyPackages.${system});

      version =
        "0.9.1"
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

      # Shared packaging for the companion tools. Both build with plain make,
      # hardcode the system path to xdg-shell.xml, and assemble their CFLAGS
      # via `CFLAGS +=`, which a command-line value would discard.
      mkTool = pkgs: { pname, src, extraBuildInputs, installPhase, description }:
        pkgs.stdenv.mkDerivation {
          inherit pname version src;

          nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config pkgs.wayland pkgs.wayland-scanner ];
          buildInputs = [ pkgs.wayland pkgs.wayland-protocols pkgs.cairo pkgs.pango ] ++ extraBuildInputs;

          preBuild = ''
            substituteInPlace Makefile \
              --replace-fail "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" \
              "${pkgs.wayland-protocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
          '';

          # Export CFLAGS so the Makefiles' `CFLAGS +=` lines still apply; this
          # also drops their -march=native. `make clean` guarantees a
          # from-scratch build even if a host-built binary leaked into the
          # source.
          buildPhase = ''
            runHook preBuild
            export CFLAGS="-O3 -DNDEBUG -Wall -Wextra -pedantic"
            make clean
            make
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
          src = ./.;

          nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config ];
          buildInputs = [ pkgs.wlroots_0_20 ] ++ wlrootsPcDeps pkgs;

          enableParallelBuilding = true;

          # CFLAGS is passed on the command line so it replaces the Makefile's
          # -march=native while keeping -O2 -DNDEBUG. The -Wno-error flags
          # silence FORTIFY 3 diagnostics (format-truncation, unused-result)
          # that would otherwise trip the Makefile's -Werror.
          buildPhase = ''
            runHook preBuild
            make CC="$CC" CFLAGS="-O2 -DNDEBUG -Wno-error=format-truncation -Wno-error=unused-result" LDFLAGS="$LDFLAGS"
            runHook postBuild
          '';

          # The root Makefile has no install target; mirror the PKGBUILD layout.
          installPhase = ''
            runHook preInstall
            install -Dm755 uwm "$out/bin/uwm"
            install -Dm644 uwm.desktop "$out/share/wayland-sessions/uwm.desktop"
            runHook postInstall
          '';

          meta = commonMeta pkgs "uwm" "A minimal BSP tiling Wayland compositor built on wlroots";
        };

        default = uwm;

        ubar = mkTool pkgs {
          pname = "ubar";
          src = ./tools/ubar;
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
          src = ./tools/ulaunch;
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

          # Nix's fortified glibc (FORTIFY 3) emits -Wformat-truncation /
          # -Wunused-result diagnostics that trip the Makefile's -Werror; the
          # user's system glibc never sees them.
          NIX_HARDENING_ENABLE = "all -fortify3";

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
          ];

          shellHook = ''
            echo "UWM dev shell: make (ASAN=1 for the sanitizer build) | make -C tools/ubar | make -C tools/ulaunch"
          '';
        };
      });

      formatter = eachSystem (system: pkgs: pkgs.nixpkgs-fmt);

      checks = eachSystem (system: pkgs:
        with self.packages.${system}; {
          inherit uwm ubar ulaunch;
        });

      nixosModules.default = { pkgs, ... }: {
        environment.systemPackages = with self.packages.${pkgs.stdenv.hostPlatform.system}; [
          uwm
          ubar
          ulaunch
        ];
      };
    };
}
