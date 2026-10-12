#!/bin/sh
# Release build of the macOS app: build, sign with Developer ID, notarize, staple, check.
#   app/release-macos.sh
# Needs, on this Mac:
#   - a "Developer ID Application" certificate in the keychain (Xcode > Settings > Accounts > Manage Certificates)
#   - notarytool credentials stored under a profile name (default blue-baton-notary):
#       xcrun notarytool store-credentials blue-baton-notary --apple-id <Apple ID> --team-id <team ID>
# Only the certificate kind and the profile name appear here; names and passwords stay in the keychain.
set -e
cd "$(dirname "$0")"
PROFILE="${NOTARY_PROFILE:-blue-baton-notary}"
export APPLE_SIGNING_IDENTITY="${APPLE_SIGNING_IDENTITY:-Developer ID Application}"
# Update files (.app.tar.gz + .sig for the in-app updater) are signed with the Tauri update key, kept outside the repo
KEY="${TAURI_UPDATE_KEY_FILE:-$HOME/.tauri/blue-baton-updater.key}"
[ -f "$KEY" ] && export TAURI_SIGNING_PRIVATE_KEY="$(cat "$KEY")" TAURI_SIGNING_PRIVATE_KEY_PASSWORD="${TAURI_SIGNING_PRIVATE_KEY_PASSWORD:-}"

npm run build -- --config src-tauri/tauri.release.conf.json

APP="src-tauri/target/release/bundle/macos/Blue Baton.app"
VERSION=$(node -p "require('./package.json').version")
DMG=$(ls src-tauri/target/release/bundle/dmg/Blue\ Baton_"$VERSION"_*.dmg | head -1)

codesign --verify --deep --strict --verbose=2 "$APP"
xcrun notarytool submit "$DMG" --keychain-profile "$PROFILE" --wait
# The DMG's notarization covers the app inside it, so both can carry the ticket
xcrun stapler staple "$DMG"
xcrun stapler staple "$APP"
spctl --assess --type execute --verbose "$APP"
spctl --assess --type open --context context:primary-signature --verbose "$DMG"
echo "Release ready: $DMG"
