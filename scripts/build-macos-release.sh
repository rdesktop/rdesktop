#!/bin/bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/build-macos-release.sh --identity IDENTITY [options]

Build rdesktop-mac.app from configure and Makefile, sign it with a Developer ID
Application certificate, and optionally notarize and staple it.

Required:
  --identity NAME       Exact codesign identity name or SHA-1 hash

Options:
  --deployment-target V Minimum macOS version (default: 11.0 for Apple Silicon)
  --output-dir DIR      Output directory (default: dist)
  --notary-profile NAME Keychain profile configured with notarytool store-credentials
  --jobs N              Parallel make jobs (default: detected CPU count)
  -h, --help            Show this help

Example:
  scripts/build-macos-release.sh \\
    --identity "Developer ID Application: Your Name (TEAMID)" \\
    --notary-profile rdesktop-notary
USAGE
}

fail() { printf 'error: %s\n' "$*" >&2; exit 2; }

identity="${CODESIGN_IDENTITY:-}"
deployment_target="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
output_dir="dist"
notary_profile=""
jobs=""

while (($#)); do
  case "$1" in
    --identity) (($# >= 2)) || fail "--identity needs a value"; identity="$2"; shift 2 ;;
    --deployment-target) (($# >= 2)) || fail "--deployment-target needs a value"; deployment_target="$2"; shift 2 ;;
    --output-dir) (($# >= 2)) || fail "--output-dir needs a value"; output_dir="$2"; shift 2 ;;
    --notary-profile) (($# >= 2)) || fail "--notary-profile needs a value"; notary_profile="$2"; shift 2 ;;
    --jobs) (($# >= 2)) || fail "--jobs needs a value"; jobs="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) fail "unknown option: $1 (use --help)" ;;
  esac
done

[[ "$(uname -s)" == Darwin ]] || fail "this release script must run on macOS"
[[ -n "$identity" ]] || fail "pass --identity or set CODESIGN_IDENTITY"
[[ "$deployment_target" =~ ^[0-9]+(\.[0-9]+){1,2}$ ]] || fail "invalid deployment target: $deployment_target"
[[ -x ./configure && -f ./Makefile.in ]] || fail "run this script from the repository root"
command -v xcrun >/dev/null || fail "Xcode command line tools are required"
command -v codesign >/dev/null || fail "codesign is required"

if [[ -z "$jobs" ]]; then
  jobs="$(sysctl -n hw.ncpu)"
fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be a positive integer"

if ! security find-identity -v -p codesigning | grep -Fq "$identity"; then
  fail "no installed code-signing identity matches '$identity'"
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
output_dir="$(mkdir -p "$output_dir" && cd "$output_dir" && pwd)"

# The vendored static libraries are architecture-specific. Rebuild them with
# the same deployment target as the app on every release build.
make clean
if [[ -f deps/nla2/deps/openssl-3.0.15/Makefile ]]; then
  make -C deps/nla2/deps/openssl-3.0.15 clean
fi
if [[ -f deps/openssl-3.0.15/Makefile ]]; then
  make -C deps/openssl-3.0.15 clean
fi
if [[ -f deps/nla2/Makefile ]]; then
  make -C deps/nla2 clean
fi

export MACOSX_DEPLOYMENT_TARGET="$deployment_target"
./configure CFLAGS="-O2 -mmacosx-version-min=$deployment_target"
make -j"$jobs"
make bundle

app_path="$output_dir/rdesktop-mac.app"
rm -rf "$app_path"
ditto rdesktop-mac.app "$app_path"

codesign --force --options runtime --timestamp \
  --sign "$identity" "$app_path/Contents/MacOS/rdesktop-mac"
codesign --force --options runtime --timestamp --sign "$identity" "$app_path"
codesign --verify --deep --strict --verbose=2 "$app_path"

if [[ -n "$notary_profile" ]]; then
  archive_path="$output_dir/rdesktop-mac-notarization.zip"
  ditto -c -k --keepParent "$app_path" "$archive_path"
  xcrun notarytool submit "$archive_path" --keychain-profile "$notary_profile" --wait
  xcrun stapler staple "$app_path"
  xcrun stapler validate "$app_path"
  rm -f "$archive_path"
fi

final_zip="$output_dir/rdesktop-mac.zip"
rm -f "$final_zip"
ditto -c -k --keepParent "$app_path" "$final_zip"
printf 'Built and signed app: %s\n' "$app_path"
printf 'Distribution archive: %s\n' "$final_zip"
