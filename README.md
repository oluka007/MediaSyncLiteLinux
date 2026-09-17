## iBroadcast Media Sync Lite v0.4.2

A Linux-native graphical uploader for iBroadcast

## Build
### Requirements overview
- gcc
- make
- gtk+3.0 development files >= 3.10
- libcurl
- openssl
- libssl-dev
- libjansson (also available in ./jansson-2.7)

### Debian
The easist way to install for Debian is using the DEB package available in releases.

You can build the package manually as well:
```bash
sudo apt install build-essential libgtk-3-dev libcurl4-openssl-dev libjansson-dev
```
### Fedora
```bash
sudo dnf groupinstall "C Development Tools and Libraries"
sudo dnf install jansson-devel libcurl-devel gtk3-devel
```

## Compile
Just execute:
```bash
make
```

## Run
Now you can run MediaSync Lite from the directory where it was built:
```bash
./mediasynclite
```

## Install
You can optionally install MediaSync Lite so it's available system-wide:
```bash
sudo make install
```
By default this installs the binary to `/usr/bin`, along with a desktop
entry and application icon (see [Desktop / start menu integration](#desktop--start-menu-integration)
below). Pass `PREFIX=/usr/local` (or another prefix) to `make install` to
install elsewhere.

## Nix
Nix users on the unstable branch can install by adding `pkgs.mediasynclite` to their system packages or in an ephemeral shell by
```nix
nix-shell -p mediasynclite
```

Alternatively, you can clone this directory and run `nix-build` to build the derivation.

## Duplicate detection & tagging
MediaSync Lite now reads basic audio metadata (ID3v2/ID3v1 tags for MP3-family
files, Vorbis comments for FLAC/Ogg/Opus) while scanning:
- The file list and upload screens show a friendlier "Artist - Title" label
  next to the path when tags are available.
- In addition to MD5-based duplicate detection (comparing against the
  hashes iBroadcast already has on file), a normalized
  title/artist/album/track "signature" is used as a secondary duplicate
  check, catching re-encoded or re-tagged copies of a track that a pure
  MD5 comparison would miss.
- Duplicate lookups now use hash sets instead of linear scans, so scanning
  large libraries against a large existing collection is significantly
  faster.
- Files uploaded during the current run are recorded immediately, so
  scanning the same folder again in the same session correctly skips them
  as duplicates without needing to restart the app.

## Known issues
Tag-based duplicate detection only covers files uploaded earlier in the
current app run (the iBroadcast API used here only exposes existing
server-side MD5 hashes, not full track metadata) - it does not retroactively
catch re-tagged duplicates of files uploaded in a previous session.

## Desktop / start menu integration
Running `make install` (or installing the `.deb` package) registers
MediaSync Lite with your desktop environment so it shows up with its icon
in the application menu / dock, like any other installed app:
- A [freedesktop.org `.desktop` entry](share/applications/mediasynclite.desktop)
  is installed to `$(PREFIX)/share/applications/mediasynclite.desktop`.
- An application icon is installed at every standard
  [hicolor icon theme](https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html)
  size (16 to 256px) under `$(PREFIX)/share/icons/hicolor/<size>/apps/mediasynclite.png`,
  so it renders crisply regardless of the desktop's icon size or scaling.
- On systems using the Debian package, `desktop-file-utils` and
  `hicolor-icon-theme` triggers refresh the menu/icon caches automatically
  on install; when installing manually via `make install` outside of a
  package manager, the Makefile also calls `update-desktop-database` and
  `gtk-update-icon-cache` itself (if available) so the icon shows up
  immediately without needing to log out/in.
- A man page (`man mediasynclite`) is also installed.

## Packaging: build a `.deb`
A Debian/Ubuntu package can be built directly from source:
```bash
sudo apt install build-essential debhelper devscripts \
    libcurl4-openssl-dev libgtk-3-dev libjansson-dev libssl-dev openssl
dpkg-buildpackage -us -uc -b
```
This produces `../mediasynclite_<version>_<arch>.deb`, which bundles the
binary, the desktop entry, and the full icon set described above. You can
sanity-check the resulting package with:
```bash
dpkg-deb -c ../mediasynclite_*_*.deb   # list package contents
lintian ../mediasynclite_*_*.deb       # validate packaging quality
```

## Continuous integration
Every push and pull request is automatically built and validated by the
[`Build & Validate Package`](.github/workflows/build-and-validate.yml)
GitHub Actions workflow, which:
1. Compiles the application with `make`.
2. Validates `share/applications/mediasynclite.desktop` with
   `desktop-file-validate`.
3. Confirms the full hicolor icon set is present.
4. Runs `make install` into a scratch `DESTDIR` and confirms the binary,
   desktop entry, and icon land in the correct paths.
5. Builds a `.deb` with `dpkg-buildpackage` and inspects its contents.
6. Validates the built `.deb` with `lintian` (failing the build on any
   `lintian` error or warning).
7. Installs the built `.deb` with `apt-get` to confirm it installs cleanly.
8. Uploads the built `.deb` as a downloadable workflow artifact
   (kept for 14 days).

### Publishing a release
Pushing a version tag matching `v*.*.*` (e.g. `v0.4.3`) additionally
triggers the `publish-release` job, which downloads the `.deb` produced by
the build job and publishes it as a **GitHub Release** with the package
attached as a downloadable asset — this is the persistent, user-facing way
to distribute the package (unlike the 14-day workflow artifact above).

To cut a release:
```bash
git tag v0.4.3
git push origin v0.4.3
```
The release will appear at
`https://github.com/<owner>/<repo>/releases/tag/v0.4.3` once the workflow
completes, with release notes auto-generated from merged PRs/commits since
the previous tag.
