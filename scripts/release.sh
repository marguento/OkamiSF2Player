#!/bin/zsh
# Builds, signs, packages and notarizes a release of Okami SF2 Player.
#
#   scripts/release.sh            -> dist/Okami-SF2-Player-<version>.pkg (signed, notarized, stapled)
#
# Needs, in the login keychain: "Developer ID Application" and "Developer ID Installer"
# certificates. Notarization uses an App Store Connect API key: NOTARY_KEY (the .p8 path),
# NOTARY_KEY_ID and NOTARY_ISSUER, from the environment or from scripts/release.env (ignored
# by git: account identifiers never go in the repo).
set -euo pipefail

cd "$(dirname "$0")/.."
VERSION=$(sed -nE 's/^project\(SF2Player VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
: ${VERSION:?could not read the version from the project() line in CMakeLists.txt}
NAME="Okami SF2 Player"
DIST=dist
[[ -f scripts/release.env ]] && source scripts/release.env
: ${NOTARY_KEY:?set NOTARY_KEY (scripts/release.env)} ${NOTARY_KEY_ID:?set NOTARY_KEY_ID} ${NOTARY_ISSUER:?set NOTARY_ISSUER}

identity() { security find-identity -v ${=2:-} | sed -nE "s/.*\"($1: [^\"]+)\".*/\1/p" | head -1; }
APP_ID=$(identity "Developer ID Application" "-p codesigning")
PKG_ID=$(identity "Developer ID Installer")
[[ -n $APP_ID ]] || { echo "No \"Developer ID Application\" certificate in the keychain."; exit 1; }
[[ -n $PKG_ID ]] || { echo "No \"Developer ID Installer\" certificate in the keychain."; exit 1; }
[[ -f $NOTARY_KEY ]] || { echo "No App Store Connect key at $NOTARY_KEY."; exit 1; }
echo "Version $VERSION, signing as: $APP_ID / $PKG_ID"

# 1. Build (universal) and test both halves. A failing test stops the release.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >/dev/null
cmake --build build
./build/PluginTests_artefacts/Release/PluginTests
arch -x86_64 ./build/PluginTests_artefacts/Release/PluginTests   # Intel, under Rosetta

# auval checks the registered AU, which may be an older installed copy (or macOS's cache of
# one): refresh the registry, then the reported version must be this one. The output goes to a
# file so a failure shows why.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
for arch in arm64 x86_64; do
    log="build/auval-$arch.txt"
    if ! arch -$arch auval -v aumu Sf2p Mrgt > "$log" 2>&1 || ! grep -q "AU VALIDATION SUCCEEDED" "$log"; then
        cat "$log"; echo "auval failed ($arch)"; exit 1
    fi
    if ! grep -q "Component Version: $VERSION " "$log"; then
        grep "Component Version" "$log"
        echo "auval validated another copy than $VERSION: remove older ones from /Library/Audio/Plug-Ins and ~/Library/Audio/Plug-Ins"
        exit 1
    fi
done

# 2. Copy the bundles out of the build and sign them: hardened runtime, secure timestamp,
#    no get-task-allow (PLAYBOOK security checklist, macOS 17).
rm -rf "$DIST" && mkdir -p "$DIST/root/Library/Audio/Plug-Ins/Components" "$DIST/root/Library/Audio/Plug-Ins/VST3"
ART=build/SF2Player_artefacts/Release
ditto "$ART/AU/$NAME.component" "$DIST/root/Library/Audio/Plug-Ins/Components/$NAME.component"
ditto "$ART/VST3/$NAME.vst3" "$DIST/root/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
for bundle in "$DIST/root/Library/Audio/Plug-Ins/Components/$NAME.component" "$DIST/root/Library/Audio/Plug-Ins/VST3/$NAME.vst3"; do
    codesign --force --options runtime --timestamp --sign "$APP_ID" "$bundle"
    codesign --verify --deep --strict "$bundle"
    if codesign -d --entitlements - --xml "$bundle" 2>/dev/null | grep -q get-task-allow; then
        echo "$bundle carries get-task-allow"; exit 1
    fi
done

# Nothing personal ships: no home folder path, e-mail or account identifier in the bundles,
# the history or the commits (scripts/privacy-check.sh; its personal patterns in release.env).
zsh scripts/privacy-check.sh "$DIST/root/Library/Audio/Plug-Ins/Components/$NAME.component" "$DIST/root/Library/Audio/Plug-Ins/VST3/$NAME.vst3"

# 3. The installer: both plugins into /Library/Audio/Plug-Ins, signed. Not relocatable, or
#    Installer would update a copy found elsewhere (a developer build, a moved bundle) instead.
pkgbuild --analyze --root "$DIST/root" "$DIST/components.plist" >/dev/null
for i in 0 1; do plutil -replace "$i.BundleIsRelocatable" -bool NO "$DIST/components.plist"; done   # the key is absent until set
pkgbuild --root "$DIST/root" --component-plist "$DIST/components.plist" --identifier com.marguento.okamisf2player \
         --version "$VERSION" --install-location / "$DIST/plugins.pkg"
PKG="$DIST/Okami-SF2-Player-$VERSION.pkg"
productbuild --package "$DIST/plugins.pkg" --sign "$PKG_ID" --timestamp "$PKG"
rm "$DIST/plugins.pkg" "$DIST/components.plist"

# 4. Notarize and staple, so it installs without warnings, even offline. notarytool can exit 0
#    for a rejected submission: read its status, and print Apple's log when it isn't Accepted.
result=$(xcrun notarytool submit "$PKG" --key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER" \
         --wait --output-format json)
submission=$(plutil -extract id raw -o - - <<< "$result")
notarized=$(plutil -extract status raw -o - - <<< "$result")
if [[ $notarized != Accepted ]]; then
    echo "Notarization: $notarized"
    xcrun notarytool log "$submission" --key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER"
    exit 1
fi
xcrun stapler staple "$PKG"
spctl --assess --type install --verbose "$PKG"

shasum -a 256 "$PKG" | tee "$PKG.sha256"
echo "Ready: $PKG"
