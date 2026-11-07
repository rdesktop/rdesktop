# Building the Linux command-line client

This fork retains the Linux `rdesktop` command-line client alongside the native
macOS application. `configure` selects Linux from the configured host OS, and
`make` builds `rdesktop` using the X11 frontend, bundled NLA and static OpenSSL,
and system GnuTLS, Nettle, Hogweed, GMP, and libtasn1 libraries. Apple frameworks
and Objective-C GUI objects are excluded from this build.

## Ubuntu and Debian dependencies

On Debian or Ubuntu, install the compiler, build tools, and development libraries:

```sh
sudo apt update
sudo apt install build-essential pkg-config autoconf automake \
  libx11-dev libxcursor-dev libxrandr-dev libkrb5-dev \
  libgnutls28-dev nettle-dev libgmp-dev libtasn1-6-dev zlib1g-dev xwayland \
  libtool
```

This is the consolidated package list for building and running the client on
a Wayland desktop. XWayland is needed for Wayland sessions; Xrandr is optional.
X11 and Xcursor are required. `zlib1g-dev` supplies `zlib.h` for the bundled
NLA engine; install it if compilation reports that header is missing. Autoconf and Automake provide
`autoconf`, `autoheader`, and `aclocal` for regenerating configure files after
changes to `configure.ac`.

### Ubuntu GSSAPI and Kerberos setup

`libkrb5-dev` supplies GSSAPI for the default CredSSP/NLA support. Kerberos
setup prompts configure runtime authentication; completing them does not
install the development files needed to build.

If configure reports `checking for krb5-gssapi... no`, install the packages
listed above and verify that `pkg-config` can find GSSAPI:

```sh
pkg-config --modversion krb5-gssapi
```

Then retry `./configure` and `make` as shown below.

On another Ubuntu installation, the user also encountered Kerberos configuration
prompts and completed them using the local domain, leaving the remaining fields
blank. These are environment-specific settings; for a managed Kerberos domain,
use the realm and server settings supplied by your administrator.

To regenerate the client and bundled NLA Autotools files and configure on Linux:

```sh
./autogen-linux.sh --prefix=/your/prefix
make
```

The script forwards arguments to `configure` and respects compiler settings such
as `CC`, `CFLAGS`, and `LDFLAGS` from the environment. Use
`NOCONFIGURE=1 ./autogen-linux.sh` to regenerate files without configuring.
Regeneration requires Libtool in addition to Autoconf, Automake, and pkg-config.
>>>>>>> ab3bdf6d ( - Work on updating the Linux code to something more modern eventually)

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
including the authenticated identity check. A user has also confirmed a live
Windows connection using an explicit username and prompted password.

## Run

Run from an X11 desktop session or a Wayland session with XWayland enabled:

```sh
./rdesktop -u username -p - server:3389
```

Run `./rdesktop -h` for command-line options, or `man rdesktop` after installation.
The client takes connection options on the command line and displays the remote
desktop in an X11 window. In a Wayland session, XWayland provides this window;
the client does not yet have a native Wayland frontend.

### Authentication with newer Windows systems

Newer Windows systems commonly require Network Level Authentication (NLA).
For these connections, explicitly pass the Windows account username with `-u`
and use `-p -` to prompt for its password. The bundled NLAv2 engine handles
CredSSP authentication before the remote desktop or Windows login screen is
available; do not rely on entering credentials at that screen.

```sh
./rdesktop -u username -p - server:3389
```

Use the Windows account password, not its Windows Hello PIN. The client does
not automatically prompt when `-p` is omitted: it sends an empty password,
which can cause an NLA logon failure. Using `-p -` also keeps the password out
of the command line and shell history.

For a domain account, specify the domain with `-d DOMAIN`:

```sh
./rdesktop -u username -d DOMAIN -p - server:3389
```

For a local Windows account, use the Windows computer name with `-d` if needed:

```sh
./rdesktop -u username -d WINDOWS_COMPUTER_NAME -p - server:3389
```

`ERRCONNECT_LOGON_FAILURE` during the NLA exchange indicates an authentication
failure. Check the account name, domain, password, and account's Remote Desktop
access. If the log already shows a completed TLS connection and exchanged NLA
tokens, the subsequent generic suggestion about old TLS versions does not
describe that failure.

### Wayland sessions

Install XWayland using the dependency command above and enable it in your
desktop environment.

On native Linux builds, `configure` automatically checks for `Xwayland` in
`PATH`; no `--enable` option is needed. If it is missing, configuration warns
and continues because XWayland is only needed at runtime for Wayland sessions.
Cross builds skip this runtime check.

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

If the client reports:

```text
UI(error): ui_init(), failed to open X11 display: wayland-0
```

`DISPLAY` contains a Wayland socket name instead of an X11 display. From a
terminal inside your Ubuntu desktop session, check the display and local X11
sockets:

```sh
echo "$DISPLAY"
ls /tmp/.X11-unix/
```

If the listing includes `X0`, try the corresponding X11 display `:0`:

```sh
DISPLAY=:0 ./rdesktop -u username -p - server:3389
```

Include the equals sign in `DISPLAY=:0`; `DISPLAY:0` is not a shell variable
assignment. Use `./rdesktop` to run the executable from this checkout rather
than another installed version. Run it as your desktop user without `sudo`.

Use `:1` for `X1`, and so on. The socket must belong to your desktop session,
and X11 authentication must still succeed. If there are no X11 sockets, install
XWayland using the dependency command above, then log out and back in and
check again. The preceding `Autoselecting keyboard map 'en-us' from locale` message
is normal and unrelated to the display failure.

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
