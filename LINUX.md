# Building the Linux command-line client

This fork retains the Linux `rdesktop` command-line client alongside the native
macOS application. `configure` selects Linux from the configured host OS, and
`make` builds `rdesktop` using the X11 frontend and system OpenSSL, GnuTLS,
Nettle, Hogweed, GMP, and libtasn1 libraries. Apple frameworks and Objective-C
GUI objects are excluded from this build.

## Dependencies

On Debian or Ubuntu, install the compiler, build tools, and development libraries:

```sh
sudo apt install build-essential pkg-config autoconf automake \
  libx11-dev libxcursor-dev libxrandr-dev libssl-dev \
  libgnutls28-dev nettle-dev libgmp-dev libtasn1-6-dev
```

Xrandr is optional; X11 and Xcursor are required. Autoconf and Automake provide
`autoconf`, `autoheader`, and `aclocal` for regenerating configure files after
changes to `configure.ac`.

## Build and install

From the repository root:

```sh
./configure --disable-smartcard --disable-credssp
make
sudo make install
```

The executable is `./rdesktop`. Installation defaults to `/usr/local`; use
`./configure --prefix=/your/prefix --disable-smartcard --disable-credssp` to
choose another location. `make install` also installs the keymaps and manual page.

This configuration has been compiled on Linux and its command-line usage screen
has been checked. A live RDP connection was not tested as part of that build
verification.

## Run

Run from an X11 desktop session, or an environment that provides an X11 display:

```sh
./rdesktop -u username server:3389
```

Run `./rdesktop -h` for command-line options, or `man rdesktop` after installation.
The client takes connection options on the command line and displays the remote
desktop in an X11 window; it is not a headless client.

## Optional features and current limits

The verified build disables smart-card and CredSSP support. Smart-card support
requires PCSC-lite development files (for example, `libpcsclite-dev`) and has not
been verified in this Linux build.

CredSSP is enabled by default in configure, so keep `--disable-credssp` for the
verified Linux build. The bundled NLA dependency still needs Linux portability
work. Installing Kerberos development dependencies alone does not resolve that
work. For future CredSSP development, Debian/Ubuntu packages include:

```sh
sudo apt install krb5-config libkrb5-dev
```

Servers that require Network Level Authentication (NLA) cannot be used with
CredSSP disabled.

`make bundle` is a macOS-only target. Use separate checkouts for Linux and macOS
builds: generated OpenSSL assembly, dependency configuration, and object files
are platform-specific and should not be copied between them.
