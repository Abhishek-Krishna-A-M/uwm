# Maintainer: Abhishek Krishna A M abhishekkrishna2k6@gmail.com

pkgname=('uwm' 'ubar')
pkgver=0.1.0
pkgrel=1
pkgdesc="A minimal BSP tiling Wayland compositor built on wlroots"
arch=('x86_64')
url="https://github.com/Abhishek-Krishna-A-M/uwm"
license=('custom:MIT')
makedepends=('git' 'pkgconf' 'wayland-protocols')
source=("${pkgname}::git+${url}.git")
sha256sums=('SKIP')

pkgver() {
  cd "${srcdir}/uwm"
  git describe --long --tags 2>/dev/null | sed 's/\([^-]*-g\)/r\1/;s/-/./g' || echo "${pkgver}"
}

build() {
  cd "${srcdir}/uwm"
  make
  make -C tools/ubar
}

package_uwm() {
  depends=(
    'libinput'
    'wayland-server'
    'wlroots>=0.20'
    'xkbcommon'
  )
  optdepends=(
    'foot: default terminal (footclient)'
    'fuzzel: application launcher and dmenu'
    'grim: screenshot utility'
    'slurp: region selection for screenshots'
    'wl-clipboard: clipboard (wl-copy) for screenshots'
    'lf: file manager'
    'fd: file search for findfile binding'
    'swaybg: wallpaper background'
    'xdg-desktop-portal: desktop integration portals'
    'pipewire: audio volume control (wpctl)'
    'brightnessctl: backlight brightness control'
    'neovim: editor used in findfile binding'
    'ubar: status bar'
  )

  cd "${srcdir}/uwm"
  install -Dm755 uwm "${pkgdir}/usr/bin/uwm"
  install -Dm644 uwm.desktop "${pkgdir}/usr/share/wayland-sessions/uwm.desktop"
  install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/uwm/LICENSE" 2>/dev/null || true
}

package_ubar() {
  depends=(
    'cairo'
    'pango'
    'pangocairo'
    'wayland-client'
  )

  cd "${srcdir}/uwm/tools/ubar"
  install -Dm755 ubar "${pkgdir}/usr/bin/ubar"
  install -Dm644 ../../LICENSE "${pkgdir}/usr/share/licenses/ubar/LICENSE" 2>/dev/null || true
}
