# Aerial Wallpaper for KDE Plasma

An unofficial Plasma 6 wallpaper that downloads Apple Aerial videos to a local
cache and plays them through Qt Multimedia. This project is not affiliated with
or endorsed by Apple.

The initial implementation uses the tvOS compatibility catalog and defaults to
1080p H.264 SDR. It briefly uses two media decoders to crossfade between local
videos and uses one serialized download at a time. Videos are muted and are
never handed to Qt Multimedia until an atomic download has completed.

## Build

Required development dependencies are CMake, Extra CMake Modules, Plasma 6,
KF6 Archive, Qt 6 Core, Concurrent, Network, QML, Multimedia runtime support,
and Qt Test.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

When installed, `sccache` is detected and enabled automatically. Disable it
with `-DAERIAL_USE_SCCACHE=OFF`, or provide another launcher through
`-DCMAKE_CXX_COMPILER_LAUNCHER=...`.

Install into the active system prefix:

```sh
sudo cmake --install build
```

On most distributions, configure with `-DCMAKE_INSTALL_PREFIX=/usr` so Plasma
and its QML engine discover both the wallpaper and native module:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
sudo cmake --install build
```

For a layout-only test without changing the desktop installation:

```sh
cmake --install build --prefix /tmp/aerial-wallpaper-install
```

The native QML module must be installed as well as the wallpaper package, so a
standalone package installed only with `kpackagetool6` is not sufficient.

## Runtime Notes

- A working Qt Multimedia FFmpeg backend and H.264 decoder are required.
- Crossfade can be disabled with **0 ms** to avoid overlapping video decoders.
  For freezes after a kernel/driver update, see the
  [software-decoding workaround](docs/graphics-troubleshooting.md).
- The first run downloads the catalog, then a complete selected video. This can
  take several minutes depending on the connection.
- Cache data is stored through `QStandardPaths` and defaults to a 4 GiB limit.
- Apple endpoints are unofficial and can change without notice. The last valid
  catalog and completed videos remain usable offline.
- Normal builds require TLS certificate validation. Fix the system CA store if
  Apple HTTPS requests fail instead of weakening production installations.

For temporary diagnosis only, certificate verification can be compiled out:

```sh
cmake -S . -B build-insecure -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DAERIAL_ALLOW_INSECURE_TLS=ON
cmake --build build-insecure -j2
sudo cmake --install build-insecure
```

This still requires HTTPS and permits only `sylvan.apple.com`, but it cannot
prove that the server is Apple. Do not distribute an insecure build. Reinstall
a normal build after diagnosing the certificate store.

See `docs/` for the architecture, source research, and staged roadmap.
