# ws2tcp-local Qt GUI

This is a Qt Widgets GUI that calls the `ws2tcp-local-ffi` C ABI layer.
The GUI stores user settings with `QSettings` and loads them automatically on
the next startup.

On Windows, enable **Set system proxy** to point the current user's WinINet
proxy at the configured local listener while ws2tcp-local is running. The
previous Windows proxy settings are restored when the proxy stops or the
application exits. A saved recovery record also allows the application to
restore settings after an unexpected exit, without overwriting proxy settings
that another program changed in the meantime.

On macOS, **Set system proxy** applies the local listener as the HTTP and HTTPS
proxy for each enabled network service. macOS may request administrator
authorization when applying or restoring network settings. The application
itself continues to run as the signed-in user. Existing per-service settings
are saved and restored, with the same unexpected-exit and ownership safeguards
as the Windows implementation.

## Build

By default CMake uses Corrosion to build the `ws2tcp-local` CLI from
`../ws2tcp-local` and builds the Rust FFI crate from `../ws2tcp-local-ffi`
with Cargo. If the local FFI crate is missing, CMake asks Cargo to fetch
`ws2tcp-local-ffi` from crates.io and builds the downloaded registry source.
Cargo also downloads the crate's transitive crates.io dependencies.

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix target/install
```

For a release build:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

On Windows, CMake also creates a `package` target backed by CPack and NSIS.
The installer contains both `ws2tcp-local-qt.exe` and the Corrosion-built
`ws2tcp-local.exe`. Build it with a release configuration to produce the
installer in the build directory. The installer offers options to create a
desktop shortcut and add the application directory to `PATH`. NSIS must be
installed and available to CPack:

```powershell
cmake -S . -B build-release
cmake --build build-release --config Release --target package
```

On macOS, the `package` target creates a GUI-only drag-and-drop DMG. The app
bundle contains the required Qt frameworks and plugins; the command-line
binary is intentionally not installed in the DMG:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target package
```

### macOS signing and privileged helper

The macOS app contains a privileged helper that updates system proxy settings.
Both the app and the helper must be signed by the same Apple Developer team;
an ad-hoc signature cannot register the helper with `SMAppService`.

First confirm that macOS sees a valid code-signing identity:

```bash
security find-identity -v -p codesigning
```

An identity consists of a certificate and its matching private key. If an
Apple Development or Developer ID certificate is present in Keychain Access
but the command reports no valid identities, check that its private key is
also present and install the current Apple Worldwide Developer Relations
(WWDR) intermediate certificate from the Apple Developer certificate page.

For local development, configure CMake with an **Apple Development** identity.
Use the complete identity name printed by `security find-identity`:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DWS2TCP_MACOS_SIGNING_IDENTITY="Apple Development: NAME (TEAM_ID)"
cmake --build build-release
cmake --install build-release --prefix "$PWD/target/install"
```

The install step is required: it deploys the Qt frameworks and Cocoa platform
plugin into the app before signing the nested code, privileged helper, and
outer bundle in the correct order. Do not distribute or install
`build-release/ws2tcp-local.app` directly; that build-tree bundle can still
refer to Homebrew Qt libraries and its temporary path can become part of the
helper registration.

Verify and install the resulting bundle:

```bash
codesign --verify --deep --strict --verbose=2 \
  target/install/ws2tcp-local.app
codesign -d --verbose=4 target/install/ws2tcp-local.app

ditto target/install/ws2tcp-local.app /Applications/ws2tcp-local.app
open /Applications/ws2tcp-local.app
```

On first use of **Set system proxy**, approve `ws2tcp-local` in **System
Settings > General > Login Items > Allow in the Background**, then retry the
operation. Always run the installed `/Applications` copy when testing the
privileged helper. Rebuilding, re-signing, or moving an already registered app
changes its code identity or path and can require the helper to be registered
and approved again.

Public releases should sign the app with a **Developer ID Application**
certificate, enable the hardened runtime and secure timestamp, and submit the
resulting DMG with `xcrun notarytool` before stapling its notarization ticket.

## GitHub Actions

`build.yml` builds Windows x64 and macOS arm64 packages for pushes to `main`,
pull requests, and manual runs. `release.yml` runs for `v*` tags, notarizes the
macOS package, and publishes the Windows installer and macOS DMG together in a
single GitHub Release.

Configure these repository Actions secrets before pushing a release tag:

- `MACOS_CERTIFICATE_P12`: base64-encoded Developer ID Application `.p12`
- `MACOS_CERTIFICATE_PASSWORD`: password used when exporting the `.p12`
- `MACOS_SIGNING_IDENTITY`: full `Developer ID Application: ... (TEAM_ID)` name
- `APPLE_ID`: Apple Developer account email
- `APPLE_APP_PASSWORD`: app-specific password used by `notarytool`
- `APPLE_TEAM_ID`: Apple Developer team ID

If the CLI crate is not in the default sibling directory, pass its path:

```bash
cmake -S . -B build \
  -DWS2TCP_LOCAL_CLI_SOURCE_DIR=/path/to/ws2tcp-local
```

If the FFI crate is not in the default sibling directory, pass its path:

```bash
cmake -S . -B build \
  -DWS2TCP_LOCAL_FFI_SOURCE_DIR=/path/to/ws2tcp-local-ffi \
  -DWS2TCP_LOCAL_FFI_INCLUDE_DIR=/path/to/ws2tcp-local-ffi/include
cmake --build build
```

To fetch a different published FFI crate version from crates.io:

```bash
cmake -S . -B build \
  -DWS2TCP_LOCAL_FFI_SOURCE_DIR=/path/that/does/not/exist \
  -DWS2TCP_LOCAL_FFI_CRATE_VERSION=0.1.2
cmake --build build
```

You can still use a prebuilt static FFI library by passing its path:

```bash
cmake -S . -B build \
  -DWS2TCP_LOCAL_FFI_AUTO_BUILD=OFF \
  -DWS2TCP_LOCAL_FFI_LIBRARY="$PWD/../ws2tcp-local-ffi/target/debug/libws2tcp_local_ffi.a"
cmake --build build
cmake --install build --prefix target/install
```

On macOS and MinGW use `libws2tcp_local_ffi.a`. With MSVC,
`WS2TCP_LOCAL_FFI_LIBRARY` must point to `ws2tcp_local_ffi.lib`. The FFI is
linked into the executable, so `ws2tcp_local_ffi.dll` is no longer deployed.
The install step still runs Qt's deployment logic on Windows, including the
`qwindows` platform plugin.
