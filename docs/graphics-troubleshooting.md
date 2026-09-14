# Freezes after a kernel or graphics driver update

Aerial plays inside `plasmashell`. Hardware decoding and video texture import
can exercise driver paths that a static wallpaper does not use. If the GPU
hangs, the wallpaper's playback timeout cannot recover the desktop.

During investigation on kernel `7.2.4-3-cachyos`, previous-boot journals showed
AMDGPU `device lost from bus!`, SMU bus errors, MES timeouts, and KWin atomic
modeset failures. Some display errors also occurred before the user session.
These establish a graphics-stack failure, but do not establish whether video
decoding, rendering, or another driver operation triggered it.

## Reduce the video driver's involvement

On a working session (use the known-working LTS kernel if necessary):

1. In Aerial's wallpaper settings, choose **1080p H.264 SDR** and set
   **Crossfade** to **0 ms**. Zero disables overlapping playback: the active
   player's source is released before the next video is opened. A brief neutral
   background between videos is expected.
2. For systemd-managed Plasma sessions, install the supplied user-service
   override from the repository root:

   ```sh
   mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/plasma-plasmashell.service.d"
   cp docs/plasma-aerial-software-decoding.conf \
     "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/plasma-plasmashell.service.d/aerial-software-decoding.conf"
   systemctl --user daemon-reload
   ```

3. Log out and back in to apply it before Qt Multimedia initializes. Installing
   or rebuilding the wallpaper alone does not enable this override.

The override selects Qt's FFmpeg backend, disables hardware video decoding,
and disables hardware texture conversion. The comma in
`QT_FFMPEG_DECODING_HW_DEVICE_TYPES=,` is intentional: Qt documents it as an
empty device list. These settings affect Qt Multimedia throughout the Plasma
shell and may be inherited by applications it launches. CPU usage will increase;
the compositor still uses the GPU. A separate lock-screen process is not covered.

This is a workaround to test, not a repair for kernel/firmware failures. If the
freeze persists, use the known-working kernel while the graphics regression is
investigated. Capture the failed boot's logs from the next working boot:

```sh
journalctl --list-boots
journalctl -b -1 -k --no-pager
journalctl --user -b -1 -u plasma-plasmashell.service --no-pager
```

Choose the appropriate boot index if the failed boot is not the previous one.
Compare the same wallpaper settings on the working and failing kernels.

## Remove the workaround

```sh
rm "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/plasma-plasmashell.service.d/aerial-software-decoding.conf"
systemctl --user daemon-reload
```

Log out and back in again. Crossfade can be re-enabled independently.

The environment variables are documented in Qt's
[Advanced FFmpeg Configuration](https://doc.qt.io/qt-6/advanced-ffmpeg-configuration.html)
and are private API, so their behavior should be rechecked after Qt upgrades.
