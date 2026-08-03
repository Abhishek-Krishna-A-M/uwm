{
  description = "UWM - a minimal BSP tiling Wayland compositor built on wlroots";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));

      # Every pc file referenced (transitively) by wlroots-0.20.pc, so that the
      # Makefile's `pkg-config --cflags wlroots-0.20 ...` invocation can never
      # fail with "Package not found" inside the Nix build sandbox.
      wlrootsPcDeps = pkgs: with pkgs; [
        wayland # wayland-server.pc, wayland-client.pc
        wayland-protocols # wayland-protocols.pc
        libinput # libinput.pc (propagated by wlroots anyway)
        libxkbcommon # xkbcommon.pc
        pixman # pixman-1.pc
        libdrm # libdrm.pc
        libGL # egl.pc, glesv2.pc
        libgbm # gbm.pc
        vulkan-loader # vulkan.pc
        lcms2 # lcms2.pc
        systemdLibs # libudev.pc
        seatd # libseat.pc (+ seatd daemon)
        libdisplay-info # libdisplay-info.pc
        libliftoff # libliftoff.pc
        libxcb # xcb.pc, xcb-render.pc, xcb-shm.pc, xcb-dri3.pc, xcb-present.pc, xcb-xfixes.pc, xcb-xinput.pc, xcb-composite.pc
        libxcb-render-util # xcb-renderutil.pc
        libxcb-wm # xcb-ewmh.pc, xcb-icccm.pc, xcb-res.pc
        libxcb-errors # xcb-errors.pc
      ];
    in
    {
      packages = forAllSystems (pkgs:
        let
          lib = pkgs.lib;
          stdenv = pkgs.stdenv;
          wlroots = pkgs.wlroots_0_20;
          version =
            "0.9.1"
            + lib.optionalString (self ? shortRev && self.shortRev != null) "+git.${self.shortRev}";
        in
        rec {
          default = stdenv.mkDerivation {            pname = "uwm";
            inherit version;
            src = ./.;

            nativeBuildInputs = with pkgs; [ gnumake pkg-config ];
            buildInputs = [ wlroots ] ++ wlrootsPcDeps pkgs;

            enableParallelBuilding = true;

            # CFLAGS is passed on the make command line so it overrides the
            # Makefile's non-reproducible "-march=native" while keeping its
            # intended -O2 -DNDEBUG. Nix's hardening flags reach the compiler
            # through the cc-wrapper's NIX_CFLAGS_COMPILE environment variable.
            # -Wno-error=format-truncation, -Wno-error=unused-result: Nix's
            # fortified glibc (FORTIFY 3) emits -Wformat-truncation /
            # -Wunused-result diagnostics (snprintf into path[128], unchecked
            # write()/fscanf() in cleanup paths) that the Makefile's -Werror
            # turns into errors on this toolchain. The user's toolchain
            # (no fortify) never sees them.
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

            meta = {
              description = "A minimal BSP tiling Wayland compositor built on wlroots";
              homepage = "https://github.com/Abhishek-Krishna-A-M/uwm";
              license = lib.licenses.mit;
              mainProgram = "uwm";
              platforms = lib.platforms.linux;
            };
          };

          # Convenience alias so `nix build .#uwm` works like `nix run .#uwm`.
          uwm = default;

          ubar = stdenv.mkDerivation {
            pname = "ubar";
            inherit version;
            src = ./tools/ubar;

            # wayland-scanner is a separate package in this nixpkgs; wayland's
            # own outputs (out/dev) do not ship the scanner binary.
            nativeBuildInputs = with pkgs; [ gnumake pkg-config wayland wayland-scanner ];
            buildInputs = with pkgs; [
              wayland
              wayland-protocols
              cairo
              pango
              pulseaudio # libpulse.pc
              pipewire # libpipewire-0.3.pc
              systemdLibs # libudev.pc
            ];

            # The Makefile hardcodes /usr/share/wayland-protocols, which does
            # not exist in the Nix sandbox; point it at the store instead.
            preBuild = ''
              substituteInPlace Makefile \
                --replace-fail "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" \
                "${pkgs.wayland-protocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
            '';

            # The tools' Makefiles assemble their flags via `CFLAGS += ...`,
            # which is suppressed for command-line-set CFLAGS, so export it in
            # the environment instead (the `?=` in their Makefiles honours it).
            # `make clean` first: tools/ubar is a nested git repo, so its
            # host-built ubar binary is invisible to the parent repo's git
            # status and ends up in the flake source; make would consider it
            # up-to-date and never compile, shipping the host binary (linked
            # against /lib64/ld-linux) instead.
            buildPhase = ''
              runHook preBuild
              export CFLAGS="-O3 -DNDEBUG -Wall -Wextra -pedantic"
              make clean
              make
              runHook postBuild
            '';

            # ubar's Makefile has no install target; install manually.
            installPhase = ''
              runHook preInstall
              install -Dm755 ubar "$out/bin/ubar"
              runHook postInstall
            '';

            meta = {
              description = "Status bar for the UWM Wayland compositor";
              license = lib.licenses.mit;
              mainProgram = "ubar";
              platforms = lib.platforms.linux;
            };
          };

          ulaunch = stdenv.mkDerivation {
            pname = "ulaunch";
            inherit version;
            src = ./tools/ulaunch;

            # wayland-scanner is a separate package in this nixpkgs; wayland's
            # own outputs (out/dev) do not ship the scanner binary.
            nativeBuildInputs = with pkgs; [ gnumake pkg-config wayland wayland-scanner ];
            buildInputs = with pkgs; [
              wayland
              wayland-protocols
              cairo
              pango
              libxkbcommon
            ];

            preBuild = ''
              substituteInPlace Makefile \
                --replace-fail "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" \
                "${pkgs.wayland-protocols}/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
            '';

            # Same `CFLAGS +=` caveat as ubar: export in the environment.
            # `make clean` is defensive only: ulaunch's binary is already
            # gitignored, but cleaning guarantees a from-scratch build.
            buildPhase = ''
              runHook preBuild
              export CFLAGS="-O3 -DNDEBUG -Wall -Wextra -pedantic"
              make clean
              make
              runHook postBuild
            '';

            # ulaunch's Makefile has a real install target honouring PREFIX.
            installPhase = ''
              runHook preInstall
              make install PREFIX="$out"
              runHook postInstall
            '';

            meta = {
              description = "Application launcher for the UWM Wayland compositor";
              license = lib.licenses.mit;
              mainProgram = "ulaunch";
              platforms = lib.platforms.linux;
            };
          };
        });

      apps = forAllSystems (pkgs:
        let
          packages = self.packages.${pkgs.stdenv.hostPlatform.system};
        in
        {
          default = {
            type = "app";
            program = "${packages.default}/bin/uwm";
          };

          uwm = {
            type = "app";
            program = "${packages.default}/bin/uwm";
          };

          ubar = {
            type = "app";
            program = "${packages.ubar}/bin/ubar";
          };

          ulaunch = {
            type = "app";
            program = "${packages.ulaunch}/bin/ulaunch";
          };
        });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          name = "uwm-dev-shell";

          # Nix's glibc enables FORTIFY_SOURCE=3, whose warn_unused_result /
          # format-truncation diagnostics trip the Makefile's -Werror; the
          # user's system glibc (no fortify) never sees them. Match the
          # user's toolchain in the dev shell; packages keep full hardening.
          NIX_HARDENING_ENABLE = "all -fortify3";

          # Pull in every compile-time dependency of the compositor and its
          # companion tools (nativeBuildInputs and buildInputs of each).
          inputsFrom = with self.packages.${pkgs.stdenv.hostPlatform.system}; [
            default
            ubar
            ulaunch
          ];

          # The shell's default `cc` comes from its stdenv (gcc), matching the
          # user's system toolchain. Raw gcc/clang are intentionally absent:
          # pkgs.clang ships a `cc` symlink that would hijack the Makefile's
          # $(CC) and fail its -Werror on clang-only diagnostics.
          nativeBuildInputs = with pkgs; [
            gnumake
            pkg-config
            git
            gdb
            bear
            valgrind
            clang-tools
            nixpkgs-fmt
          ];

          # Runtime companions needed to actually run and test the compositor.
          buildInputs = with pkgs; [
            swaybg
            mako
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

      checks = forAllSystems (pkgs: {
        build = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
        ubar = self.packages.${pkgs.stdenv.hostPlatform.system}.ubar;
        ulaunch = self.packages.${pkgs.stdenv.hostPlatform.system}.ulaunch;
      });

      formatter = forAllSystems (pkgs: pkgs.nixpkgs-fmt);

      nixosModules.default = { pkgs, ... }: {
        environment.systemPackages = with self.packages.${pkgs.stdenv.hostPlatform.system}; [
          default
          ubar
          ulaunch
        ];
      };
    };
}
