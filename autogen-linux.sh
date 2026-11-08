#!/bin/sh
# Regenerate the Linux build system, then configure unless NOCONFIGURE is set.
set -eu

# Resolve paths relative to this script so it can be invoked from any directory.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir"

for tool in autoreconf autoconf autoheader aclocal automake libtoolize pkg-config; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required tool: $tool" >&2
        echo "Debian/Ubuntu: apt install autoconf automake libtool pkg-config" >&2
        exit 1
    fi
done

# autoreconf orders libtoolize, aclocal, autoconf and autoheader correctly.
# Force regeneration without using autoupdate, which rewrites configure.ac.
echo "Regenerating Linux build files..."
autoreconf --force --install
(
    cd deps/nla2
    autoreconf --force --install
)

if [ -n "${NOCONFIGURE:-}" ]; then
    echo "Regeneration complete. Run ./configure, then make to build ./rdesktop."
else
    echo "Configuring rdesktop for Linux..."
    ./configure "$@"
    echo "Configuration complete. Run make to build ./rdesktop."
fi
