#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 || ! -f $1 ]]; then
  printf 'Usage: %s PACKAGE.pkg.tar.zst\n' "$0" >&2
  exit 1
fi
package=$1
pacman -Qip "$package"
[[ $(pacman -Qp "$package") == mediasynclite-git\ * ]]
contents=$(pacman -Qqlp "$package")
for path in /usr/bin/mediasynclite /usr/share/applications/mediasynclite.desktop /usr/share/mediasynclite/ui.glade; do
  grep -Fxq "$path" <<< "$contents" || { printf 'Missing: %s\n' "$path" >&2; exit 1; }
done
for size in 16x16 22x22 24x24 32x32 48x48 64x64 128x128 256x256; do
  path="/usr/share/icons/hicolor/$size/apps/mediasynclite.png"
  grep -Fxq "$path" <<< "$contents" || { printf 'Missing: %s\n' "$path" >&2; exit 1; }
done
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
bsdtar -xf "$package" -C "$stage" usr/share/applications/mediasynclite.desktop
desktop-file-validate "$stage/usr/share/applications/mediasynclite.desktop"
printf 'PASS: package identity, binary, UI, desktop entry and all eight icon sizes\n'
