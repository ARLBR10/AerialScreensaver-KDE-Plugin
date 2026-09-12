# AGENTS.md

## Project

This repository implements an unofficial Apple Aerial wallpaper plugin for KDE
Plasma 6. The root project is the product. `Aerial-Base/` and
`Plasma-Video-Base/` are read-only reference submodules, not implementation
directories.

## Non-Negotiable Boundaries

- Keep the implementation independent. Do not copy code from the GPL-licensed
  `Plasma-Video-Base/` unless the repository intentionally changes license and
  records that decision.
- Do not edit either reference submodule for product changes.
- Do not commit Apple videos, thumbnails, extracted resource archives, cache
  files, or generated build output.
- Treat Apple endpoints and schemas as volatile, untrusted inputs.
- Require HTTPS and validate redirect hosts for every remote resource.
- Never expose a partial download to Qt Multimedia. Download to a temporary
  file and atomically activate it before returning a local file URL.
- Never perform network, archive, or large file operations synchronously on the
  QML/UI thread.
- Keep Apple manifest keys in the C++ backend. QML consumes only normalized
  asset/model roles and compact IDs.
- Use one `MediaPlayer` per wallpaper instance. Crossfade and multiple
  simultaneous decoders are intentionally deferred.
- Apple Aerials are always muted.

## Architecture

- `package/`: Plasma `WallpaperItem` package, configuration schema, and QML.
- `plugin/`: process-local QML singleton, catalog model, downloads, selection,
  and cache management.
- `tests/`: parser/model tests and small metadata fixtures only.
- `docs/`: source research, architecture, and staged implementation plan.

The backend owns remote URLs, catalog snapshots, cache paths, downloads, and
format selection. Plasma configuration stores only user choices such as asset
IDs, quality policy, playback order, fill mode, and cache limit.

## Runtime Policy

- Default quality is 1080p H.264 SDR (`compatibility`).
- Prefer complete cached files and remain usable offline.
- Preserve the last valid catalog when refresh or parsing fails.
- Bound retries and advance after media errors; never reload one known-bad file
  in a tight loop inside `plasmashell`.
- Keep cache size bounded and never evict the file currently playing.
- Use `QStandardPaths`; do not hard-code home or distribution paths.
- Design for multiple screens, activities, previews, and a separate lock-screen
  process. Shared mutable state must live in the backend singleton.

## Code Style

- Target C++20, Qt 6.6+, KF6, and Plasma 6.
- Prefer the smallest direct implementation over speculative abstractions.
- Use Qt parent ownership and RAII. Avoid blocking event loops and nested event
  loops.
- Validate data at trust boundaries. Ignore unknown JSON fields, treat empty
  strings as missing, and reject assets without a valid HTTPS media variant.
- Keep comments sparse and explain constraints rather than syntax.
- Use ASCII unless an existing file requires Unicode.

## Build And Verify

Configure and build out of tree:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Install to a temporary prefix when validating package layout:

```sh
cmake --install build --prefix /tmp/aerial-wallpaper-install
```

Validate the package directly when `kpackagetool6` is available:

```sh
kpackagetool6 --type Plasma/Wallpaper --install package
```

Use a low parallel build count by default because this plugin is expected to be
developed on desktop systems where `plasmashell` may already be active.

## Change Checklist

- Add or update tests for parser, selection, download, or cache behavior.
- Exercise malformed and missing input, not only the happy path.
- Confirm QML receives only local file URLs for playback.
- Confirm cancellation and process interruption cannot create a valid-looking
  partial cache file.
- Confirm errors leave a neutral fallback visible and do not cause retry loops.
- Run the build and tests above. Report unavailable dependencies or untested
  desktop integration explicitly.
