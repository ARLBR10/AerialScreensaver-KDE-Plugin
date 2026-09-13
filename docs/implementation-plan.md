# Implementation Plan

## Phase 0: Decisions And Probes

Deliverables:

- Select project name, reverse-DNS plugin ID, and license.
- Confirm minimum Plasma, Qt, and KF6 versions on target distributions.
- Build a standalone Qt Multimedia probe that plays representative Apple files:
  1080p H.264 SDR, 1080p HEVC SDR, 4K HEVC SDR, and 4K HDR.
- Measure codec support, hardware decoding, CPU/GPU load, memory, seek behavior,
  end-of-media signaling, and playback-rate behavior.
- Capture current source documents as small redacted test fixtures; do not add
  video files to git.
- Verify whether current tar archives can be read by `KTar` and where
  `entries.json` and localization bundles occur.

Exit criteria:

- At least 1080p H.264 SDR plays reliably through Qt Multimedia on each primary
  target distribution.
- The chosen plugin license is compatible with all code intended for reuse.
- A current macOS and tvOS catalog can be parsed from fixtures.

## Phase 1: Playback Skeleton

Deliverables:

- Plasma 6 wallpaper package with valid `metadata.json`.
- `WallpaperItem` root with one `MediaPlayer`, muted `AudioOutput`, and
  `VideoOutput`.
- Configuration for one local video and fit/fill mode.
- End-of-media loop/advance behavior.
- Player error fallback and journal diagnostics.
- Desktop and lock-screen installation targets.

Exit criteria:

- The plugin appears in Desktop and Wallpaper settings.
- A local `.mov` plays without hiding desktop icons.
- Pause/resume and Plasma restart do not lose valid configuration.
- A bad file renders the fallback background and does not create a crash loop.

## Phase 2: Catalog Backend

Deliverables:

- Native QML module and `AerialBackend` singleton.
- Source registry with discovery URL, fallback archive URL, manifest family,
  and enabled state.
- Safe asynchronous resource download and tar staging.
- tvOS and macOS parser implementations producing the normalized model.
- Duplicate merge and localized-name fallback.
- `QAbstractListModel` catalog exposed to configuration QML.
- Atomic source snapshot activation and offline fallback.

Exit criteria:

- Fixture tests cover missing fields, empty strings, unknown fields, malformed
  JSON, duplicate IDs, unsafe tar entries, and an interrupted refresh.
- A failed refresh leaves the previous catalog unchanged.
- No network or archive operation blocks the QML/UI thread.

## Phase 3: Download And Cache

Deliverables:

- Variant selector with explicit fallback reason.
- Download queue with progress, cancellation, temporary files, and atomic
  completion.
- Checksum verification when supplied.
- Cache index, size accounting, pinning, and LRU eviction.
- Storage management UI and clear-cache action.
- Download-on-demand from catalog selections.

Exit criteria:

- Cancelling or killing Plasma during a download cannot produce a playable
  partial file.
- Current media is never evicted.
- Cache size converges to the configured limit after downloads finish.
- Offline startup plays an already cached selection.
- Disk-full and checksum-failure paths are tested.

## Phase 4: Aerial Rotation

Deliverables:

- Catalog browsing by source/category with search and multi-selection.
- Sequential and shuffled-queue playback.
- Persisted current asset/cursor.
- Preview display while a video is being prepared.
- Automatic next download with one-item lookahead.
- Source refresh controls and last-refresh/error status.

Exit criteria:

- A selected group rotates without immediate repeats.
- Removing or disabling the current asset advances safely.
- A stale media URL causes one source refresh and bounded recovery.
- Configuration stores IDs/policies, not a full copy of the catalog.

## Phase 5: Plasma Lifecycle And Power

Deliverables:

- Pause for inactive activity, locked/off screen, configured battery threshold,
  and maximized/full-screen windows.
- Per-monitor filtering where Plasma APIs permit it.
- Context actions for Next and Pause.
- Resource-use documentation and codec troubleshooting.

Exit criteria:

- Decode utilization falls to idle while paused.
- Switching activities and lock/unlock cycles resumes predictably.
- Multiple monitor instances do not race downloads or corrupt cache state.
- Lock-screen instances perform no unrequested network access.

## Phase 6: Packaging And Release

Deliverables:

- Source tarball and reproducible build instructions.
- Distribution dependency matrix for at least Arch, Fedora, openSUSE, and one
  Debian/Ubuntu-family target.
- Automated unit tests and package validation in CI.
- Privacy, content-source, licensing, and troubleshooting documentation.
- Recovery instructions for removing broken wallpaper configuration without
  deleting unrelated Plasma settings.

Exit criteria:

- Clean install, upgrade, and uninstall are tested.
- No Apple media is included in release artifacts.
- The source list can be updated without changing QML playback code.
- At least one full day of playback passes on each primary test system without
  unbounded memory/cache growth.

## Deferred Work

- Synchronized multi-monitor playback.
- HDR enabled by default.
- macOS 240 fps slow-motion parity.
- Time-of-day and solar scheduling beyond simple metadata filters.
- Location/point-of-interest overlays.
- Weather and now-playing overlays.
- Custom folders, community expansion sources, and live feeds.
- A standalone daemon for downloads that survive `plasmashell` restarts.

## Test Strategy

### Unit tests

- Discovery plist parsing and URL validation.
- Both manifest families and schema-tolerance cases.
- Asset merge and variant provenance.
- Format selection across capability matrices.
- Stable cache keys and index migration.
- LRU eviction with pinned/current files.
- Archive path validation.

### Integration tests

- Local HTTP server with redirects, range support, missing length, timeout,
  truncated body, changed ETag, and HTTP errors.
- Archive download, extraction, validation, and atomic activation.
- Interrupted and resumed/restarted video downloads.
- Offline launch from a cached catalog and cached video.
- QML model updates while configuration UI opens and closes.

### Manual matrix

Test at minimum:

| Area | Cases |
| --- | --- |
| Session | Wayland; X11 only if the selected Plasma baseline still supports it. |
| GPU | Intel, AMD, Nvidia where available. |
| Backend | Qt FFmpeg default and GStreamer fallback where packaged. |
| Codec | H.264, HEVC, HDR rejection/fallback, high-frame-rate playback. |
| Displays | One display, mixed-resolution displays, hotplug, per-screen wallpaper. |
| Lifecycle | Restart Plasma, logout/login, suspend/resume, lock/unlock, activity switch. |
| Network | Fast, slow, offline, captive/failed DNS, server response changed. |
| Storage | Cache limit reached, low disk space, read-only path, clear while idle. |

## Initial Configuration Contract

Keep the first `main.xml` intentionally small:

```text
EnabledSourceIds       StringList
SelectedAssetIds       StringList
QualityPolicy          String     (compatibility, 1080-sdr, 4k-sdr, hdr)
PlaybackOrder          String     (shuffle, sequential)
FillMode               Int
PauseForFullscreen     Bool
PauseOnBattery         Bool
BatteryThreshold       Int
CacheLimitMiB          Int
PinnedAssetIds         StringList
LastAssetId            String
QueueSeed              String
QueueCursor            Int
```

Large operational state, URLs, progress, and catalog entries belong in backend
storage, not KConfig.

## Open Decisions

These do not block documentation, but Phase 0 must resolve them:

1. Plugin name and ID: whether to use `Aerial` in the public name and how to
   make the unofficial nature clear.
2. License: MIT for an independent implementation, or GPL-2.0-or-later if code
   is incorporated from Smart Video Wallpaper Reborn.
3. Baseline versions: broad Plasma 6 support versus newer APIs and a smaller
   support matrix.
4. Source defaults: tvOS compatibility catalog first, macOS high-frame-rate
   catalog first, or both with a setup choice.
5. Cache semantics: whether user-pinned videos live in cache or durable app
   data and how uninstall should treat them.
6. Packaging: source/KDE Store package only or maintained distro packages for
   the native QML module and multimedia dependencies.

## First Coding Slice

After these docs are accepted, the smallest end-to-end implementation should
be:

1. Create the Plasma package and native QML module build.
2. Parse a checked-in minimal tvOS fixture into a two-row catalog model.
3. Select one fixture asset whose media URL is supplied by a local test server.
4. Download atomically to the Qt cache directory.
5. Play the resulting local file in `VideoOutput`.
6. Advance to the second item at end of media.
7. Show a fallback background on a forced download or codec error.

That slice validates every architectural boundary without first building the
full settings browser or relying on Apple's live service during tests.
