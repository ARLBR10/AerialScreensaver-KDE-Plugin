# Architecture

## Goals

- Integrate as a native Plasma 6 desktop and lock-screen wallpaper.
- Keep network and disk operations asynchronous so `plasmashell` remains
  responsive.
- Continue playing cached content when Apple is unavailable.
- Contain Apple-specific schemas behind a normalized catalog API.
- Avoid decoding multiple copies of the same video unnecessarily.
- Fail to a still preview or neutral background instead of crash-looping
  Plasma when media playback fails.

## Package Shape

Proposed repository layout after the documentation phase:

```text
CMakeLists.txt
package/
  metadata.json
  contents/
    config/main.xml
    ui/main.qml
    ui/config.qml
    ui/VideoPlayer.qml
    ui/components/...
plugin/
  CMakeLists.txt
  plugin.cpp
  aerialbackend.{h,cpp}
  catalogmodel.{h,cpp}
  manifestparser.{h,cpp}
  sourceprovider.{h,cpp}
  downloadmanager.{h,cpp}
  cachemanager.{h,cpp}
tests/
  fixtures/...
  unit/...
```

`metadata.json` must declare:

```json
{
  "KPackageStructure": "Plasma/Wallpaper",
  "KPlugin": {
    "Id": "org.kde.plasma.aerial",
    "Name": "Aerial",
    "License": "project-license",
    "Version": "0.1.0"
  },
  "X-KDE-ParentApp": "org.kde.plasmashell",
  "X-Plasma-API-Minimum-Version": "6.0"
}
```

The final ID and license must be selected before implementation. The package is
installed with `plasma_install_package(...)`; the native types are installed as
a QML module using `ecm_add_qml_module(...)`.

## Runtime Components

### QML wallpaper

Responsibilities:

- Root item is `WallpaperItem` from `org.kde.plasma.plasmoid`.
- Own active and standby `MediaPlayer`, muted `AudioOutput`, and `VideoOutput`
  pairs so the next local video can be decoded before transition.
- Select aspect fill, fit, or stretch.
- Advance when `MediaPlayer.EndOfMedia` is reached.
- Surface `onErrorOccurred` and request a different compatible item.
- Pause for configured power, lock, screen, activity, and window conditions.
- Expose contextual actions such as Next, Pause, and Open Settings.
- Render a cached preview or neutral background while loading and on failure.

Crossfade briefly uses two simultaneous decoders and more GPU/video memory. The
standby player is assigned only a completed local file, starts near the end of
the active video, and is released after the transition.

### QML configuration

Responsibilities:

- Show sources and refresh status.
- Browse normalized catalog entries with cached preview images.
- Filter and select assets without storing the full catalog in KConfig.
- Select quality policy, playback order, cache size, and pause behavior.
- Trigger/cancel downloads and show progress/errors.
- Warn before large or HDR/high-frame-rate downloads.

The configuration stores compact preferences such as selected asset IDs and
quality policy. It must not serialize hundreds of full remote URLs into
`plasma-org.kde.plasma.desktop-appletsrc`.

### C++ QML backend

Use Qt/KDE facilities:

- `QNetworkAccessManager` for discovery, manifests, previews, and video files.
- `QSaveFile` or temporary files plus atomic rename for durable writes.
- `QJsonDocument` and `QJsonObject` for manifests.
- `QXmlStreamReader` or `QSettings` in plist-compatible mode only after fixture
  tests confirm the discovery plist shape.
- `KArchive`/`KTar` from KF6 Archive, with explicit safe-extraction checks, for
  resource tar files.
- `QAbstractListModel` for catalog and download views.
- `QStandardPaths` for all user storage.

Expose a narrow interface to QML:

```text
AerialBackend
  state, errorMessage, catalogModel, downloadModel
  refreshSources(force)
  selectedPlayableUrl(assetIds, qualityPolicy)
  ensureDownloaded(assetId, qualityPolicy)
  cancelDownload(assetId)
  markPlaying(cacheKey, playing)
  clearCache()
```

All methods that touch the network or large files must return immediately.
Signals/model updates deliver progress. JSON and catalog parsing can run in a
worker thread; QObjects and models are updated only on their owning thread.

## Process And Instance Model

Plasma may create a wallpaper instance per screen, activity, configuration
preview, and lock screen. Do not assume a single `main.qml` instance.

Within one process, register the backend as a QML singleton so screens share
catalog and download state. Protect cache state with a process-level lock. A
lock-screen process must be treated as separate and should use already cached
files rather than start network downloads.

For the MVP, one screen can independently rotate through the selected list.
Synchronized multi-monitor playback is a later feature because independent
players do not start on the same frame. A future coordinator can publish one
asset ID and monotonic start time to all wallpaper instances.

A separate background daemon is not required initially. Reconsider one if
downloads must survive `plasmashell` restarts or if doing archive/catalog work
inside the shell causes measurable reliability issues.

## Storage

Resolve paths with `QStandardPaths`, not hard-coded home-directory strings.
Expected Linux locations are conceptually:

```text
GenericDataLocation/<plugin-id>/
  catalogs/<source-id>/active/...
  catalog-index.json

GenericCacheLocation/<plugin-id>/
  previews/...
  videos/...
  staging/...
  cache-index.json
```

Durable user selections remain in Plasma's wallpaper configuration. Source
snapshots belong in application data because they provide the offline catalog.
Regenerable previews and videos belong in cache unless users pin downloads for
offline use; pinned files may need a durable data location.

The cache index should include a schema version and be rebuildable by scanning
files. Corrupt index data must not make cached videos inaccessible.

## Playback State Machine

```text
Idle
  -> ResolvingSelection
  -> WaitingForDownload
  -> Loading
  -> Playing
  -> Paused
  -> Advancing
  -> Loading

Any state -> RecoverableError -> ResolvingSelection
Any state -> NoPlayableContent -> FallbackBackground
```

Rules:

- Only pass a completed local file URL to `MediaPlayer` in the MVP.
- Keep the current file pinned against eviction from `Loading` through `Idle`.
- Retry another variant of the same asset after a codec/media error.
- Retry another asset after all variants fail.
- Bound retries to avoid a tight failure loop in `plasmashell`.
- Record failures by asset variant and backend for diagnostics.
- Reset a failure after catalog revision changes or the user requests retry.

## Selection

Selection is deterministic from these inputs:

```text
configured asset IDs
source/category filters
day/night policy
shuffle seed or sequential cursor
format policy
known failed variants
cache availability
```

The backend returns a local URL and metadata. The QML player should not know
Apple manifest keys or construct cache paths.

For random playback, use a shuffled queue rather than choosing randomly after
every item; this avoids immediate repeats. Persist only the current asset ID
and queue seed/cursor needed to resume.

## Pause And Resource Policy

Smart Video Wallpaper Reborn demonstrates useful Plasma integrations:

- `org.kde.taskmanager` can detect visible, active, maximized, and full-screen
  windows for the current activity/desktop/screen.
- Plasma's power-management data can implement a battery threshold.
- `org.freedesktop.ScreenSaver` can report lock state.
- A wallpaper instance should stop decode work when its activity is inactive.

The initial policy should be conservative:

- Always mute Apple Aerials.
- Pause when the screen is locked/off or the activity is inactive.
- Pause on battery below a configurable threshold.
- Default to pausing behind a maximized/full-screen window.
- Release the media source after a configurable long pause if measurements show
  meaningful memory savings; restore position when playback resumes.

Window/task APIs used by wallpaper QML may not be public-stability APIs. Keep
those integrations in isolated QML components and test each supported Plasma
minor release.

## Failure Boundaries

| Failure | Required behavior |
| --- | --- |
| Discovery endpoint unavailable | Use last valid source snapshot. |
| Archive/schema changed | Preserve active snapshot and show refresh error. |
| Video URL expired or returns error | Refresh source once, then try another variant/asset. |
| Download interrupted | Retain resumable metadata if supported; otherwise safely replace `.part`. |
| Disk full | Stop download, preserve existing cache, and show actionable error. |
| Missing codec | Mark variant failed and use compatibility fallback. |
| Qt Multimedia error/crash risk | Never auto-reload the same known-bad variant in a tight loop. |
| No network and empty cache | Show preview if cached, otherwise a neutral explanatory background. |

## Dependencies

Expected build dependencies:

- CMake and Extra CMake Modules.
- Plasma 6 development files.
- Qt 6 Core, Qml, Quick, Network, Multimedia, and Test.
- KF6 I18n and Archive.

Runtime playback also requires a working Qt Multimedia backend and system
codecs. Distribution package names differ. Hardware video decode is strongly
recommended for continuous 4K/HEVC playback.

## Reuse Boundary

From Aerial, concepts worth porting include source separation, tolerant
manifest models, duplicate merging, format fallback, checksum verification,
and cache rotation. Any copied MIT code must retain its copyright and license
notice.

From Smart Video Wallpaper Reborn, independently reproduce only the needed
behavior unless this project adopts GPL-2.0-or-later. Particularly useful
behavioral references are its `WallpaperItem` package metadata, Qt Multimedia
player, end-of-media rotation, power checks, task model, screen-lock handling,
and crash recovery guidance.
