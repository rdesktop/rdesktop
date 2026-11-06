# Building the Linux command-line client

This fork retains the Linux `rdesktop` command-line client alongside the native
macOS application. `configure` selects Linux from the configured host OS, and
`make` builds `rdesktop` using the X11 frontend, bundled NLA and static OpenSSL,
and system GnuTLS, Nettle, Hogweed, GMP, and libtasn1 libraries. Apple frameworks
and Objective-C GUI objects are excluded from this build.

## Dependencies

On Debian or Ubuntu, install the compiler, build tools, and development libraries:

```sh
sudo apt install build-essential pkg-config autoconf automake \
  libx11-dev libxcursor-dev libxrandr-dev libkrb5-dev \
  libgnutls28-dev nettle-dev libgmp-dev libtasn1-6-dev
```

Xrandr is optional; X11 and Xcursor are required. Autoconf and Automake provide
`autoconf`, `autoheader`, and `aclocal` for regenerating configure files after
changes to `configure.ac`.

## Build and install

From the repository root:

```sh
./configure
make
sudo make install
```

The executable is `./rdesktop`. Installation defaults to `/usr/local`; use
`./configure --prefix=/your/prefix` to choose another location. `make install` also installs the keymaps and manual page.

This configuration has been compiled on Linux and its command-line usage screen
has been checked. The bundled client/server NLA integration test also passes,
including the authenticated identity check. A live Windows RDP connection was
not tested.

## Run

Run from an X11 desktop session or a Wayland session with XWayland enabled:

```sh
./rdesktop -u username server:3389
```

Run `./rdesktop -h` for command-line options, or `man rdesktop` after installation.
The client takes connection options on the command line and displays the remote
desktop in an X11 window. In a Wayland session, XWayland provides this window;
the client does not yet have a native Wayland frontend.

### Wayland sessions

Install and enable XWayland in your desktop environment. On Debian/Ubuntu:

```sh
sudo apt install xwayland
```

`DISPLAY` identifies the X11/XWayland server (for example, `:0`), while
`WAYLAND_DISPLAY` identifies the Wayland socket (for example, `wayland-0`).
These variables should not be set to the same value. Normally your desktop
session sets both correctly.

The client first uses the configured X11 display. In a Wayland session, if
`DISPLAY` is missing or incorrectly contains the Wayland socket name, it tries
local X11 sockets owned by the current user and logs the selected display.
X11 authentication remains required. An explicitly configured X11 display is
preserved even if it fails, including SSH-forwarded displays.

If no connection succeeds, check that XWayland is running and that `DISPLAY`
and `XAUTHORITY` match your desktop session. The client does not start an
XWayland server or change the desktop session's environment.

## Optional features and current limits

Smart-card support defaults to disabled on Linux. Pass `--enable-smartcard` to
enable it explicitly; this requires PCSC-lite development files (for example,
`libpcsclite-dev`) and has not been verified in this Linux build. The macOS
default remains enabled.

CredSSP/NLA is enabled by default. The build compiles the bundled FreeRDP/WinPR
sources into `deps/nla2/libnlav2.a` and links that archive into `rdesktop`.
Linux selects WinPR's portable Unicode conversion implementation and excludes
Apple-only sources. OpenSSL is also built from the bundled sources and linked
statically, including the legacy provider needed for NTLM authentication; no
separate NLA library installation is required.

To omit CredSSP, pass `--disable-credssp`. Servers requiring Network Level
Authentication (NLA) cannot be used with CredSSP disabled.

`make bundle` is a macOS-only target. Use separate checkouts for Linux and macOS
builds: generated OpenSSL assembly, dependency configuration, and object files
are platform-specific and should not be copied between them.

## NLA integration test

Build the existing test against the same static NLA and OpenSSL libraries:

```sh
make -C deps/nla2 test_nlav2
(cd deps/nla2 && ./test_nlav2)
```

This runs a local client/server authentication exchange using test credentials;
it does not require an RDP server.
