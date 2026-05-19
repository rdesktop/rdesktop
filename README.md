# 📣 rdesktop – Actively Maintained Fork

**The original [rdesktop/rdesktop](https://github.com/rdesktop/rdesktop) is no longer maintained**
and has not received updates for many years. It contains known security vulnerabilities.

✅ **This fork is actively maintained** and receives bug fixes, security patches and improvements.

**Use this repository instead of the original one.**

Contributions, bug reports and pull requests are welcome!

# rdesktop - A Remote Desktop Protocol client

rdesktop is an open source client for Microsoft's RDP protocol. It is
known to work with Windows versions ranging from NT 4 Terminal Server
to Windows 2012 R2 RDS. rdesktop currently has implemented the RDP version 4
and 5 protocols.


## Installation

rdesktop uses a GNU-style build procedure.  Typically all that is necessary
to install rdesktop is the following:

	% ./configure
	% make
	% make install

The default is to install under `/usr/local`.  This can be changed by adding
`--prefix=<directory>` to the configure line.

The smart-card support module uses PCSC-lite. You should use PCSC-lite 1.2.9 or
later. To enable smart-card support in the rdesktop add `--enable-smartcard` to
the configure line.


## Build dependencies

When building from source, rdesktop requires a C compiler, GNU make, X11
headers and libraries, pkg-config, GMP, GnuTLS, Nettle, hogweed, libtasn1 and
libXcursor.

When building from a git checkout instead of a release tarball, `autoconf` and
`automake` are also required because `./bootstrap` must generate the configure
script and Makefile templates.

Optional features need additional dependencies:

- smart-card support: PCSC-lite headers and library;
- CredSSP support: GSSAPI/Kerberos headers and library;
- sound support: one or more supported audio backends such as ALSA, PulseAudio,
  libao or OSS, plus libsamplerate when available.

On Debian and Ubuntu, a typical development setup is:

	% sudo apt-get install autoconf automake gcc make pkg-config \
		libx11-dev libxcursor-dev libxrandr-dev libgmp-dev \
		libgnutls28-dev nettle-dev libtasn1-6-dev

Add optional packages when enabling those features, for example:

	% sudo apt-get install libpcsclite-dev libkrb5-dev libasound2-dev \
		libpulse-dev libao-dev libsamplerate0-dev


## Note for users building from source

If you have retrieved a snapshot of the rdesktop source, you will first
need to run `./bootstrap` in order to generate the build infrastructure.
This is not necessary for release versions of rdesktop.


## Usage

Connect to an RDP server with:

	% rdesktop server

where `server` is the name of the Terminal Services machine. By default,
rdesktop connects to the standard RDP TCP port 3389; use `server:port` to
connect to a non-standard port. If you receive
"Connection refused", this probably means that the server does not have
Terminal Services enabled, or there is a firewall blocking access.

You can also specify a number of options on the command line.  These are listed
in the rdesktop manual page (run `man rdesktop`).


## Documentation

Additional documentation is available in the `doc/` directory. In particular,
`doc/keymapping.txt` describes the keyboard mapping file format and
`doc/keymap-creation.txt` provides a practical checklist for creating or
updating keymaps.


## Troubleshooting

### `Autoselecting keyboard map ... from locale`

This line is informational, not a connection failure. It only means rdesktop
selected a keyboard map from your current locale. If the session does not open,
check the next error line for the real failure, such as an unreachable host, a
closed TCP port, a certificate prompt, or an X11 display problem.

To choose a keyboard layout explicitly, use `-k`, for example:

	% rdesktop -k ja server

### `failed to open X11 display`

rdesktop is an X11 client. If startup fails with:

	UI(error): ui_init(), failed to open X11 display:

then rdesktop could not connect to an X server. Start it from a graphical X11
session, make sure the `DISPLAY` environment variable is set, and avoid running
it from a plain TTY or from `root` without X11 access. When using SSH, enable X11
forwarding or run rdesktop on the machine that owns the graphical session.
