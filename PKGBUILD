# Maintainer: Abhishek Krishna A M abhishekkrishna2k6@gmail.com
#
# NOTE: this PKGBUILD lives in the repo root, but makepkg defaults to
# srcdir=$startdir/src and pkgdir=$startdir/pkg, which collides with this
# repo's own src/ directory (a previous in-tree run left a nested clone at
# src/uwm/). Do NOT run makepkg here. Instead build from a clean dir —
# the git source is cloned fresh, so only the PKGBUILD is needed there:
#   mkdir -p /tmp/uwm-pkg && cp PKGBUILD /tmp/uwm-pkg/ && cd /tmp/uwm-pkg && makepkg -si
# or keep everything here but redirect the build dirs, e.g.:
#   BUILDDIR=/tmp/uwm-build makepkg -si
# Either way the build clones from GitHub, so push your work first; for
# unpushed local edits install directly with `make NATIVE=0 install` instead.

pkgbase=uwm
pkgname=('uwm' 'ubar' 'ulaunch')
pkgver=1.2.0.2.ga17b8b4
pkgrel=1
pkgdesc="A minimal BSP tiling Wayland compositor built on wlroots"
arch=('x86_64' 'aarch64')
url="https://github.com/Abhishek-Krishna-A-M/uwm"
license=('MIT')
makedepends=(
  'git'
  'pkgconf'
  'wayland'           # wayland-client/server .pc files + wayland-scanner
  'wayland-protocols' # xdg-shell.xml + wayland-protocols.pc
  'wlroots0.20'       # wlroots-0.20.pc (+ Requires.private chain)
  'libinput'
  'libxkbcommon'
  'pixman'
  'libdrm'
  'cairo'
  'pango'
  'libpulse'          # ubar volume/battery backend
  'pipewire'          # ubar libpipewire-0.3
  'systemd-libs'      # ubar libudev
)
source=("${pkgbase}::git+${url}.git")
sha256sums=('SKIP')

pkgver() {
  cd "${srcdir}/${pkgbase}"
  git describe --long --tags 2>/dev/null | sed 's/^v//; s/-/./g' || echo "${pkgver}"
}

build() {
  cd "${srcdir}/${pkgbase}"
  # NATIVE=0: distro binaries must run on any machine of this architecture,
  # not just the build host (upstream Makefile defaults to -march=native).
  # WERROR=0: keep fortified-toolchain warnings from tripping -Werror.
  make NATIVE=0 WERROR=0
  make -C tools/ubar NATIVE=0
  make -C tools/ulaunch NATIVE=0
}

package_uwm() {
  depends=(
    'wlroots0.20'
    'wayland'
    'libxkbcommon'
    'libinput'
  )
  optdepends=(
    'foot: default terminal (footclient)'
    'ubar: status bar'
    'ulaunch: application launcher and dmenu'
    'grim: screenshot utility'
    'slurp: region selection for screenshots'
    'wl-clipboard: clipboard (wl-copy) for screenshots'
    'lf: file manager'
    'fd: file search for findfile binding'
    'swaybg: wallpaper background'
    'xdg-desktop-portal: desktop integration portals'
    'xdg-desktop-portal-wlr: ScreenCast/Screenshot portal backend'
    'xdg-desktop-portal-gtk: FileChooser portal backend'
    'pipewire: audio volume control (wpctl)'
    'brightnessctl: backlight brightness control'
    'neovim: editor used in findfile binding'
  )

  cd "${srcdir}/${pkgbase}"
  install -Dm755 build/uwm "${pkgdir}/usr/bin/uwm"
  install -Dm644 uwm.desktop "${pkgdir}/usr/share/wayland-sessions/uwm.desktop"
  install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/uwm/LICENSE"
}

package_ubar() {
  pkgdesc="Status bar for the UWM Wayland compositor"
  depends=(
    'cairo'
    'pango'
    'wayland'
    'libpulse'
    'pipewire'
    'systemd-libs'
  )

  cd "${srcdir}/${pkgbase}/tools/ubar"
  install -Dm755 ubar "${pkgdir}/usr/bin/ubar"
  install -Dm644 ../../LICENSE "${pkgdir}/usr/share/licenses/ubar/LICENSE"
}

package_ulaunch() {
  pkgdesc="Application launcher for the UWM Wayland compositor"
  depends=(
    'cairo'
    'pango'
    'wayland'
    'libxkbcommon'
  )

  cd "${srcdir}/${pkgbase}/tools/ulaunch"
  install -Dm755 ulaunch "${pkgdir}/usr/bin/ulaunch"
  install -Dm644 ../../LICENSE "${pkgdir}/usr/share/licenses/ulaunch/LICENSE"
}
