# Aerial Wallpaper for KDE Plasma

## Feasibility

Yes, this is doable on Plasma 6.

Apple publishes Aerial metadata archives and video files over HTTPS. A Plasma
wallpaper can play the selected local video with Qt Multimedia. The missing
piece is not playback; it is a Linux-native catalog and cache manager that can
discover Apple's current resources, normalize multiple manifest formats, and
download a suitable video safely.

This repository should implement an independent Plasma wallpaper plugin. The
macOS Aerial project is useful as a reference for source discovery, manifest
models, format fallback, and cache behavior, while Smart Video Wallpaper Reborn
is useful as a reference for proven Plasma playback and lifecycle behavior.

## Decision Summary

- Target Plasma 6 and Qt 6 only for the first release.
- Use a `WallpaperItem` QML package with `MediaPlayer` and `VideoOutput`.
- Add a small C++ QML extension for network, archive, catalog, and cache work.
- Download complete videos before playback in the MVP. Do not depend on remote
  streaming for normal operation.
- Default to 1080p H.264 SDR for compatibility, then let users opt into HEVC,
  4K, HDR, or high-frame-rate variants.
- Treat all Apple URLs and schemas as volatile inputs, not as a stable public
  API.
- Store only metadata and user choices in Plasma configuration. Keep manifests,
  previews, and videos in the user's cache/data directories.
- Do not redistribute Apple video files or thumbnails with the plugin.
- Implement the project independently unless it deliberately adopts the GPL
  terms required to copy code from Smart Video Wallpaper Reborn.

## Proposed Scope

The first usable release should provide:

- Apple macOS and tvOS source catalogs.
- Catalog refresh with an offline fallback to the last valid catalog.
- Filters by source, category, and selected videos.
- Automatic format selection with a visible fallback result.
- On-demand download, progress, cancellation, and bounded cache size.
- Random or sequential rotation at the end of a video.
- Muted playback, aspect-fill/aspect-fit, pause on battery, and pause when a
  maximized or full-screen window is present.
- Per-wallpaper-instance configuration and safe multi-monitor behavior.

Features such as overlays, weather, points of interest, live streams, custom
expansion packs, and cross-device synchronization should wait until the core
catalog/download/playback path is stable.

## Documents

- [Apple data sources](apple-data-sources.md): observed endpoints, schemas,
  normalization, and source risks.
- [Architecture](architecture.md): package layout, components, storage, and
  runtime behavior.
- [Implementation plan](implementation-plan.md): milestones, acceptance
  criteria, testing, and unresolved decisions.

## Reference Projects

- [Aerial](https://github.com/AerialScreensaver/Aerial), MIT licensed. The
  `Aerial-Base/` submodule pins the code being used as a reference in this
  repository.
- [Smart Video Wallpaper Reborn](https://github.com/luisbocanegra/plasma-smart-video-wallpaper-reborn),
  GPL-2.0-or-later and currently aimed at Plasma 6.
- [Plasma Framework wallpaper template](https://github.com/KDE/plasma-framework/tree/master/templates/plasma6-wallpaper-with-qml-extension).
- [Qt Multimedia QML playback](https://doc.qt.io/qt-6/qml-qtmultimedia-mediaplayer.html).

Research in these documents was last checked on 2026-09-09. Apple resource
locations can change without notice.
