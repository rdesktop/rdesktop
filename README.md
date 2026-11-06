# Upstream maintenance notice

The upstream rdesktop project is no longer maintained. This fork retains both
the native macOS application and the Linux command-line client. There are reported security vulnerabilities, yet those
haven't been reviewed.

Use with caution and use it at your own risk.

# rdesktop - A Remote Desktop Protocol client

rdesktop is an open source client for Microsoft's RDP protocol. It is
known to work with Windows versions ranging from NT 4 Terminal Server
to Windows 2012 R2 RDS. rdesktop currently has implemented the RDP version 4
and 5 protocols.


## Installation

`configure` detects the host platform and selects the appropriate build:

| Platform | Executable | Frontend and libraries | Build guide |
| --- | --- | --- | --- |
| macOS | `rdesktop-mac` | Native macOS GUI, Apple frameworks, bundled OpenSSL | [macOS build instructions](BUILD_MAC.md) |
| Linux | `rdesktop` | Command-line interface, X11 session window, bundled NLA and OpenSSL | [Linux build instructions](LINUX.md) |

Smart-card support defaults to disabled on Linux. CredSSP/NLA is enabled by
default and uses the bundled `libnlav2.a` authentication engine:

```sh
./configure
make
sudo make install
```

Linux supports X11 desktops and Wayland sessions through XWayland. The client
can recover a missing `DISPLAY` or one incorrectly set to the Wayland socket
name. See the [Linux build guide](LINUX.md#wayland-sessions) for display setup,
dependencies, and optional feature settings.

On macOS, the Makefile build remains:

```sh
./configure
make
make bundle
```

`make bundle` is available only on macOS. Use separate checkouts when building
on different platforms to avoid reusing platform-specific objects and bundled
dependency outputs.

The default is to install under `/usr/local`.  This can be changed by adding
`--prefix=<directory>` to the configure line.

The smart-card support module uses PCSC-lite. You should use PCSC-lite 1.2.9 or
later. To enable smart-card support in the rdesktop add `--enable-smartcard` to
the configure line.

## Usage (Linux command line)

Connect to an RDP server with:

	% rdesktop server

where `server` is the name of the Terminal Services machine. If you receive
"Connection refused", this probably means that the server does not have
Terminal Services enabled, or there is a firewall blocking access.

You can also specify a number of options on the command line.  These are listed
in the rdesktop manual page (run `man rdesktop`).

## Usage macOS - Invoke the rdesktop-mac.app Application Bundle

### Build and sign with Xcode

Open `Rdesktop-MacOS/Rdesktop-MacOS.xcodeproj` and select the `rdesktop-mac`
scheme. In the target's **Signing & Capabilities** settings, select your Apple
Developer team. The project uses automatic signing and a Developer ID
certificate for distribution. Build normally for local use, or choose
**Product > Archive** to prepare a release build. The resulting product is
`rdesktop-mac.app`.

For command-line builds, select the team and request a Release archive:

```sh
xcodebuild -project Rdesktop-MacOS/Rdesktop-MacOS.xcodeproj \
  -scheme rdesktop-mac -configuration Release \
  -archivePath build/rdesktop-mac.xcarchive \
  DEVELOPMENT_TEAM=YOUR_TEAM_ID archive
```

In Xcode's Organizer, choose **Distribute App > Direct Distribution** to
export, sign with Developer ID, and notarize the app for distribution outside
the Mac App Store. Notarization and stapling are required for the smoothest
Gatekeeper experience on other Macs. Distribute the exported app in a zip or
disk image; do not copy only the executable out of the bundle.

The target's minimum macOS version is 10.13. The app uses the architecture of
the Mac used to build it, so create separate Intel and Apple Silicon archives
if you need to support both processor families. The legacy Makefile build and
`make bundle` remain available.

### Scripted Developer ID release

For a signed build from the command line, install your **Developer ID
Application** certificate and private key in the login keychain, then run this
from the repository root:

```sh
scripts/build-macos-release.sh \
  --identity "Developer ID Application: Your Name (TEAMID)"
```

The script runs `configure`, builds with `make`, creates the app bundle, signs
the executable and app with hardened runtime and a secure timestamp, and
verifies the signature. It defaults to a macOS 11.0 deployment target for
Apple Silicon and writes `dist/rdesktop-mac.app` plus a distributable zip.
It cleans prior generated build outputs before rebuilding. Use
`--deployment-target 10.13` for an Intel build that should run on older Intel
macOS releases. Builds use the current machine's architecture.

To notarize and staple the app as part of the script, first store notarization
credentials with `xcrun notarytool store-credentials`, then pass the profile
name with `--notary-profile PROFILE`. The script submits a zip, waits for
Apple's result, staples the ticket to the app, and creates the final zip.

---- Update for rdesktop-mac port ---
SVG Icon thanks to Remote Desktop SVG Vector
https://www.svgrepo.com/svg/366419/remote-desktop

Free Download Remote Desktop SVG vector file in monocolor and multicolor type for Sketch and Figma from Remote Desktop Vectors svg vector collection. Remote Desktop Vectors SVG vector illustration graphic art design format.

    COLLECTION: Puppylinux Interface Icons
    LICENSE: PD License
    AUTHOR: puppylinux

