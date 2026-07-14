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
