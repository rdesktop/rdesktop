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


## Note for users building from source

If you have retrieved a snapshot of the rdesktop source, you will first
need to run `./bootstrap` in order to generate the build infrastructure.
This is not necessary for release versions of rdesktop.


## Usage

Connect to an RDP server with:

	% rdesktop server

where `server` is the name of the Terminal Services machine. If you receive
"Connection refused", this probably means that the server does not have
Terminal Services enabled, or there is a firewall blocking access.

You can also specify a number of options on the command line.  These are listed
in the rdesktop manual page (run `man rdesktop`).
