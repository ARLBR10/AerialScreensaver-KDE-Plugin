# Apple Data Sources

## Status Of The API

There is no documented, versioned Apple Aerial developer API. The available
interface is a collection of Apple configuration files, tar archives, JSON
manifests, localization bundles, preview images, and `.mov` assets used by
macOS and tvOS.

Consequences:

- Endpoints and filenames may change without notice.
- A successful HTTP response does not guarantee the expected schema.
- Different OS generations expose different formats and metadata.
- The application must retain the last valid catalog for offline use.
- Source discovery and parsing must be replaceable without changing playback.

## Observed Sources

The following table describes sources observed on 2026-09-09. It is research
input, not a permanent contract.

| Source | Discovery or resource URL | Notes |
| --- | --- | --- |
| macOS 26 | `https://configuration.apple.com/configurations/internetservices/aerials/resources-config-26-0.plist` | The plist currently contains a `resources-url` pointing to a versioned tar archive. Prefer following this indirection instead of embedding the tar URL. |
| tvOS 26 | `https://sylvan.apple.com/itunes-assets/Aerials126/v4/c0/45/d9/c045d9d0-9606-1535-62fe-189edb4f79eb/resources-atv-23J-2.tar` | Current Aerial reference source. No equally simple stable discovery plist has been confirmed. Keep this in an updateable source registry. |
| tvOS 16 | `https://sylvan.apple.com/Aerials/resources-16.tar` | Older but useful compatibility catalog containing multiple resolution/codec variants. |
| tvOS 13 | `https://sylvan.apple.com/Aerials/resources-13.tar` | Legacy fallback used by the current Aerial reference. |
| tvOS 11 | `https://sylvan.apple.com/Aerials/2x/entries.json` | Direct JSON, useful only as a legacy source. |

A tvOS bag request has also been observed in current systems:

```text
https://bag.itunes.apple.com/bag.xml?deviceClass=AppleTV&format=json&os=tvOS&osVersion=26.4&product=com.apple.idleassetsd&productVersion=1.0&profile=TVIdleScreen&profileVersion=1&storefront=143441-1,29
```

It should be investigated, but not made the only MVP discovery mechanism. Its
request parameters are client-profile details rather than a public API
contract, and the response may vary by OS version or storefront.

## Retrieval Pipeline

For every source refresh:

1. Fetch the discovery document when one exists.
2. Require HTTPS and an allow-listed Apple host.
3. Resolve the versioned archive URL.
4. Send conditional requests using cached `ETag` and `Last-Modified` values.
5. Download to a temporary file with explicit size and timeout limits.
6. Extract into a new staging directory, never over the active catalog.
7. Reject absolute paths, `..` traversal, links escaping the staging directory,
   excessive entry counts, and excessive expanded size.
8. Locate and parse `entries.json` plus optional localization resources.
9. Validate required fields and require at least one playable asset.
10. Atomically replace the previous source snapshot only after validation.
11. Preserve the previous valid snapshot when any step fails.

Redirects must be revalidated against the host allow-list. Initial expected
hosts are `configuration.apple.com`, `sylvan.apple.com`, and, only if bag
discovery is implemented, `bag.itunes.apple.com`.

## Manifest Families

### tvOS-style manifest

The common shape is:

```json
{
  "version": 1,
  "assets": [
    {
      "id": "asset-id",
      "accessibilityLabel": "Location",
      "pointsOfInterest": {},
      "url-1080-H264": "https://.../video.mov",
      "url-1080-SDR": "https://.../video.mov",
      "url-4K-SDR": "https://.../video.mov",
      "url-4K-HDR": "https://.../video.mov"
    }
  ]
}
```

Fields are optional in practice. Other observed URL keys include
`url-1080-HDR`, `url-4K-SDR-120FPS`, and `url-4K-SDR-240FPS`. Some manifests
also provide an MD5 key adjacent to each URL key.

### macOS-style manifest

Newer macOS manifests include `assets`, `categories`, and localization keys.
An asset can contain:

```json
{
  "id": "asset-id",
  "shotID": "shot-id",
  "localizedNameKey": "name-key",
  "accessibilityLabel": "Location",
  "previewImage": "https://.../preview.png",
  "categories": ["category-id"],
  "subcategories": ["subcategory-id"],
  "pointsOfInterest": {},
  "includeInShuffle": true,
  "showInTopLevel": true,
  "url-4K-SDR-240FPS": "https://.../video.mov"
}
```

The high-frame-rate media is designed to be played below its encoded frame
rate on Apple platforms. Playback rate behavior must be measured on Linux
before this source becomes the default. The tvOS catalog is a better initial
source for ordinary 30/60 fps, 1080p H.264, and HDR/SDR choices.

## Normalized Model

All parsers should emit one internal model rather than exposing Apple keys to
QML:

```text
Source
  id, displayName, kind, revision, fetchedAt

Asset
  id, shotId, displayName, sourceIds, categoryIds, previewUrl,
  includeInShuffle, variants[]

Variant
  url, width, height, dynamicRange, codec, nominalFrameRate,
  playbackRateHint, checksum, sourceId
```

Rules:

- Use Apple's asset `id` as the primary identity.
- Preserve `shotID` separately; it can help detect related revisions.
- Merge duplicate asset IDs across sources and retain per-variant provenance.
- Treat empty strings as missing fields.
- Ignore unknown fields so additive schema changes do not break parsing.
- Reject an asset only when it has no valid HTTPS media variant.
- Keep unrecognized URL keys in diagnostics, not in the playback model.
- Resolve display names from localization data, then
  `accessibilityLabel`, then `shotID`, then `id`.

## Format Selection

The selector receives user preference and runtime capability information, then
returns both the selected variant and a reason for any fallback.

Recommended default order:

1. Exact requested format.
2. Same resolution and dynamic range with another supported codec.
3. Same dynamic range at a lower resolution.
4. 1080p H.264 SDR as the compatibility fallback.
5. Any supported SDR variant.

Do not silently promote SDR to HDR or 1080p to 4K. HDR output through Qt
Multimedia, the selected backend, the GPU driver, and KWin must be tested as a
complete chain. Until then, label HDR experimental.

The backend should expose a capability probe and a short test clip workflow.
File extension alone is not enough to infer codec or dynamic range. Manifest
keys are the initial hint; media metadata can refine it after download.

## Download And Cache Rules

- Download to `<final-name>.part` and atomically rename after success.
- Verify a published checksum when available. MD5 is useful for corruption
  detection but is not a security boundary.
- Validate that the result is a regular file and exceeds a small sanity size.
- Prefer a stable filename derived from asset ID plus variant properties,
  rather than trusting the remote basename as unique.
- Track URL, source revision, byte count, checksum, access time, and status in
  a cache index.
- Never remove a file currently used by a player.
- Evict least-recently-used unpinned files when the configured limit is
  exceeded.
- Keep enough free disk space for the active download and extraction staging.
- Limit concurrent video downloads; two is a reasonable initial default.
- Do not begin multi-gigabyte downloads without a user-visible size estimate or
  explicit confirmation when size is unknown.

## Distribution And Legal Boundary

Aerial's source code is MIT licensed, but that does not grant rights to Apple's
videos, images, names, or service. Smart Video Wallpaper Reborn is
GPL-2.0-or-later.

For the initial project:

- Ship no Apple media or copied Apple artwork.
- Let the user's machine fetch assets directly from Apple.
- Clearly identify Apple as the content source and this project as unofficial.
- Avoid implying that the feeds are a supported public service.
- Publish a privacy statement explaining direct requests to Apple hosts.
- Obtain project-specific legal review before publishing in a store or distro.
- If code is copied from Smart Video Wallpaper Reborn, comply with its GPL
  terms. Studying behavior and implementing it independently does not require
  copying its source.

This section records engineering constraints and is not legal advice.

## References

- [Aerial source list](https://github.com/AerialScreensaver/Aerial/blob/main/ScreenSaver/Source/Models/Sources/SourceList.swift)
- [Aerial manifest models](https://github.com/AerialScreensaver/Aerial/blob/main/ScreenSaver/Source/Models/Sources/Source.swift)
- [Aerial offline mode documentation](https://github.com/JohnCoates/Aerial/blob/master/Documentation/OfflineMode.md)
- [Community-maintained feed list](https://gist.github.com/theothernt/57a51cade0c12c407f48a5121e0939d5)
