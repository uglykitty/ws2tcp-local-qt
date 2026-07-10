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
  -DWS2TCP_LOCAL_FFI_CRATE_VERSION=0.1.1
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
