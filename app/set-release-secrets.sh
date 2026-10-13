#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Registers the repository secrets that .github/workflows/release.yml needs, in one go (GitHub CLI, logged in).
#   app/set-release-secrets.sh [owner/repo]
# Reads from this Mac: the Tauri update key (~/.tauri/blue-baton-updater.key) and the name and team ID of the Developer
# ID certificate (keychain; the name is only used to mask it in the build log). Asks for: the Developer ID .p12 exported
# from Keychain Access and its password, the Apple ID, and an app-specific password. Values go to gh through stdin;
# nothing is written to disk.
set -e
REPO="${1:-enfp-dev-studio/blue-baton}"
KEY="${TAURI_UPDATE_KEY_FILE:-$HOME/.tauri/blue-baton-updater.key}"

[ -f "$KEY" ] || { echo "Update key not found: $KEY"; exit 1; }
IDENTITY=$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: .*\)"$/\1/p' | head -1)
[ -n "$IDENTITY" ] || { echo "No \"Developer ID Application\" certificate in the keychain"; exit 1; }
TEAM=$(printf '%s' "$IDENTITY" | sed -n 's/.*(\([A-Z0-9]*\))$/\1/p')
SIGNING_NAME=$(printf '%s' "$IDENTITY" | sed 's/ ([A-Z0-9]*)$//')

trap 'stty echo 2>/dev/null' EXIT INT TERM
ask() { printf '%s: ' "$1" >&2; read -r REPLY; printf '%s' "$REPLY"; }
ask_hidden() { printf '%s: ' "$1" >&2; stty -echo; read -r REPLY; stty echo; echo >&2; printf '%s' "$REPLY"; }

P12=$(ask "Developer ID .p12 file (drag it here)")
P12=$(printf '%s' "$P12" | sed "s/^['\"]//; s/['\"]$//; s/\\\\ / /g")
[ -f "$P12" ] || { echo "File not found: $P12"; exit 1; }
P12_PASS=$(ask_hidden ".p12 password")
APPLE_ID=$(ask "Apple ID email")
APP_PASS=$(ask_hidden "App-specific password (appleid.apple.com)")

put() { printf '%s' "$2" | gh secret set "$1" --repo "$REPO" >/dev/null && echo "set $1"; }
put TAURI_SIGNING_PRIVATE_KEY "$(tr -d '\n' < "$KEY")"
put APPLE_CERTIFICATE "$(base64 -i "$P12" | tr -d '\n')"
put APPLE_CERTIFICATE_PASSWORD "$P12_PASS"
put APPLE_ID "$APPLE_ID"
put APPLE_PASSWORD "$APP_PASS"
put APPLE_TEAM_ID "$TEAM"
put APPLE_SIGNING_NAME "$SIGNING_NAME"
echo "Done. You can delete the .p12 now: the certificate stays in the keychain."
