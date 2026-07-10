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

By default CMake builds the Rust FFI crate from `../ws2tcp-local-ffi` with
Cargo. If that local crate is missing, CMake asks Cargo to fetch
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
  -DWS2TCP_LOCAL_FFI_CRATE_VERSION=0.1.0
cmake --build build
```

You can still use a prebuilt FFI library by passing the built library path:

```bash
cmake -S . -B build \
  -DWS2TCP_LOCAL_FFI_AUTO_BUILD=OFF \
  -DWS2TCP_LOCAL_FFI_LIBRARY="$PWD/../ws2tcp-local-ffi/target/debug/libws2tcp_local_ffi.so"
cmake --build build
cmake --install build --prefix target/install
```

On macOS use `libws2tcp_local_ffi.dylib`. On Windows,
`WS2TCP_LOCAL_FFI_LIBRARY` must point to the generated
`ws2tcp_local_ffi.dll`, and `WS2TCP_LOCAL_FFI_IMPLIB` must point to the
generated import library used for linking when auto-build is disabled. The
install step also runs Qt's deployment logic on Windows, including the
`qwindows` platform plugin.
