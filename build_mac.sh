#!/bin/bash
set -e

# build_mac.sh
# Automates the clean build process of rdesktop-mac bundle using the nested OpenSSL.

WORKSPACE_DIR="$(pwd)"

echo "=== starting rdesktop-mac build process ==="

# 1. Run configure if Makefile doesn't exist
if [ ! -f "${WORKSPACE_DIR}/Makefile" ]; then
    echo "=> running configure..."
    ./configure
fi

# 2. Clean workspace to ensure no dirty state
echo "=> cleaning workspace..."
make clean
if [ -f deps/nla2/deps/openssl-3.0.15/Makefile ]; then
    make -C deps/nla2/deps/openssl-3.0.15 clean
fi
if [ -f deps/openssl-3.0.15/Makefile ]; then
    make -C deps/openssl-3.0.15 clean
fi
if [ -f deps/nla2/Makefile ]; then
    make -C deps/nla2 clean
fi

# 3. Build through the Xcode project so the bundle identifier, team,
#    entitlements, hardened runtime, and signing settings stay in one place.
if ! command -v codesign >/dev/null 2>&1; then
    echo "error: codesign is required to build the macOS application bundle" >&2
    exit 1
fi
if ! command -v xcodebuild >/dev/null 2>&1; then
    echo "error: xcodebuild is required to build the macOS application bundle" >&2
    exit 1
fi

CODESIGN_IDENTITY="${CODESIGN_IDENTITY:--}"
DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM:-798J6VKR27}"
XCODE_CONFIGURATION="${XCODE_CONFIGURATION:-Debug}"
XCODE_DERIVED_DATA_PATH="${XCODE_DERIVED_DATA_PATH:-${WORKSPACE_DIR}/build/xcode}"

echo "=> building with Xcode (configuration: ${XCODE_CONFIGURATION})..."

XCODEBUILD_ARGS=(
    -project Rdesktop-MacOS/Rdesktop-MacOS.xcodeproj
    -scheme rdesktop-mac
    -configuration "${XCODE_CONFIGURATION}"
    -derivedDataPath "${XCODE_DERIVED_DATA_PATH}"
    DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}"
)

if [ "${CODESIGN_IDENTITY}" = "-" ]; then
    # Xcode cannot perform automatic signing without a certificate. Build the
    # product unsigned, then apply a local ad-hoc signature below.
    XCODEBUILD_ARGS+=(CODE_SIGNING_ALLOWED=NO)
else
    if ! security find-identity -v -p codesigning | grep -Fq "${CODESIGN_IDENTITY}"; then
        echo "error: no installed code-signing identity matches '${CODESIGN_IDENTITY}'" >&2
        exit 1
    fi
    XCODEBUILD_ARGS+=(CODE_SIGNING_ALLOWED=YES CODE_SIGN_IDENTITY="${CODESIGN_IDENTITY}")
    XCODEBUILD_ARGS+=(CODE_SIGN_STYLE=Manual)
fi

xcodebuild "${XCODEBUILD_ARGS[@]}" build

BUILT_APP="${XCODE_DERIVED_DATA_PATH}/Build/Products/${XCODE_CONFIGURATION}/rdesktop-mac.app"
if [ ! -d "${BUILT_APP}" ]; then
    echo "error: Xcode did not produce ${BUILT_APP}" >&2
    exit 1
fi

rm -rf rdesktop-mac.app
ditto "${BUILT_APP}" rdesktop-mac.app

echo "=> verifying rdesktop-mac.app signing (identity: ${CODESIGN_IDENTITY})..."

if [ "${CODESIGN_IDENTITY}" = "-" ]; then
    codesign --force --sign - rdesktop-mac.app/Contents/MacOS/rdesktop-mac
    codesign --force --sign - rdesktop-mac.app
else
    # Xcode signs Developer ID builds. The strict verification below confirms
    # that the copied product remains valid after it is moved to the workspace.
    codesign --verify --deep --strict --verbose=2 rdesktop-mac.app
fi

echo "=== build process completed successfully! ==="
echo "You can run the client using: open rdesktop-mac.app"
