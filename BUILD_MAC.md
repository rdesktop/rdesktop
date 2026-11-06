# Building rdesktop-mac Natively on macOS

This document provides step-by-step instructions for building the native macOS port of `rdesktop` (`rdesktop-mac`) as a self-contained application bundle.

Linux command-line build support is retained; see [LINUX.md](LINUX.md). The
Makefile uses the host OS detected by `configure`: macOS builds `rdesktop-mac`
with native GUI objects and Apple frameworks, while Linux builds `rdesktop`
with X11 and system crypto libraries. `make bundle` is macOS-only. Use separate
checkouts for each platform to avoid reusing generated dependency outputs.

---

## 1. TLS Engine Options

The macOS port supports three compilation pathways for security and TLS handshakes:

### A. Locally Bundled Static OpenSSL 3.x (Recommended)
* **Description**: Compiles a local static version of OpenSSL 3.0.15 and links it directly into the application.
* **Pros**: Standard compliance, connection stability, and **zero runtime dependencies**. The resulting `.app` bundle is fully portable and does not require Homebrew to be installed on the client machine.

### C. Native Secure Transport Engine (Dependency-Free)
* **Description**: Uses Apple's native legacy `Security.framework` (`SSLHandshake`, `SSLRead`, `SSLWrite`) directly.
* **Pros**: Smallest binary footprint; requires zero external libraries.

---

## 2. Build Prerequisites

Ensure you have the **Xcode Command Line Tools** installed:
```bash
xcode-select --install
```

---

## 3. Build Instructions

### A. The Automated Pathway (Recommended)
We provide an automated build script `build_mac.sh` that compiles the local OpenSSL dependency, configures the workspace, builds the `libnlav2.a` authentication engine, and compiles the final application bundle in one command:

```bash
./build_mac.sh
```

### B. The Step-by-Step Manual Pathway

#### Step 3.1: Compile the Local Static OpenSSL Dependency
Navigate to the nested OpenSSL source directory, configure it for static compilation, and compile it:

```bash
cd deps/nla2/deps/openssl-3.0.15

# Configure OpenSSL for static libraries only (no shared objects, no tests, no dynamic modules, enable legacy provider)
./Configure no-shared no-tests no-module enable-legacy

# Compile OpenSSL static libraries
make -j$(sysctl -n hw.ncpu)

cd ../../../..
```

#### Step 3.2: Configure the Workspace
Generate the Makefiles for the project:

```bash
./configure
```

#### Step 3.3: Rebuild and Package the App Bundle
Clean the build directory and package the client. This compiles the static authentication engine `libnlav2.a` (which links against the nested OpenSSL), links it with `rdesktop-mac`, rasterizes the SVG icon, and assembles the `.app` bundle:

```bash
make clean
make bundle
```

This compiles `rdesktop-mac.app` in the root of the workspace.

---

## 4. Automatic App Icon Generation

When you run `make bundle`, the build system automatically converts the vector SVG icon:

1. **SVG Rasterization**: Checks for `Resources/rdesktop-mac.svg` and uses the native macOS QuickLook utility (`qlmanage`) to render a high-resolution `512x512` PNG.
2. **ICNS Packaging**: Compiles and runs `make_icns.m` (using native `AppKit` APIs) to resize the PNG into standard macOS retina configurations (`16x16` up to `512x512 @2x`) under a temporary `.iconset` directory.
3. **Compilation**: Invokes Apple's native `iconutil` compiler to output `rdesktop-mac.icns` directly into the bundle's `Contents/Resources/` folder.
4. **Registration**: Writes the `<key>CFBundleIconFile</key><string>rdesktop-mac.icns</string>` entry into the generated `Info.plist` to register the custom icon.

---

## 5. Running the Client

To launch the connection dialog graphical user interface:
```bash
open rdesktop-mac.app
```

To run directly from the command line inside the bundle:
```bash
./rdesktop-mac.app/Contents/MacOS/rdesktop-mac -u <username> -p <password> <host_ip>:3389
```

---

## 6. Developer Reference: Key Customizations

### A. Color Representation (BGRA / BGRX Native Alignment)
RDP pixel color bytes are organized in BGRX/BGRA memory layouts (`Blue, Green, Red, Alpha/Skip`).
* **The Fix**: To avoid Red/Blue color channel swapping or green/blue tinting, the macOS display contexts, bitmaps, and backing store contexts in `macwin.m` are created with:
  `kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little`
* **Why `NoneSkipFirst`**: In big-endian formats, the `Skip` byte is first. When combined with `kCGBitmapByteOrder32Little`, CoreGraphics reverses the byte order, placing the `Skip` byte at the end of the address block (`Byte 3`), perfectly matching the little-endian BGRX memory stream.

### B. Mouse Coordinate Precision
To handle High-DPI/Retina scaling and OS window clamping symmetrically:
* **The Fix**: Coordinates inside `macwin.m` mouse event handlers are converted from view-relative points using a translation matrix:
  ```objc
  double scaleX = (double)g_width / bounds.size.width;
  double scaleY = (double)g_height / bounds.size.height;
  ```
  This maps the mouse inputs accurately onto the RDP session's exact pixel matrix.

### C. Redraw Artifact Mitigation
* **The Fix**: `ui_paint_bitmap` and `ui_patblt` flag `g_view.needsRedraw = YES;` directly. This ensures the 60 FPS display refresh timer commits all new frames and pattern fills to the layer's contents instantly, eliminating drawing lag and lingering artifact blocks.

## 7. Errata
- Work on screen redraw flickering on the m4
- Work on screen artifacts and windows not being quickly repained
- When disconnecting or logging off, don't repo connection failed
- Resume research on NLMv2
- Explore other performance optimizations


