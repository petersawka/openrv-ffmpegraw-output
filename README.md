# FFmpegRaw Output Plugin

OpenRV output plugin that captures the Presentation Mode framebuffer and writes
it to a local file via a child `ffmpeg` process. The default format is
**NUT** (raw or lossless-compressed video), making it useful for frame-accurate
offline review, editorial interchange, and quality-control captures.

```
RV Presentation Mode
   │
   ▼
FFmpegRawVideoDevice::transfer(GLFBO*)
   │   glReadPixels (RGBA8 / RGBA16)
   ▼
10-frame ring buffer ──► writer thread ──► pipe ──► ffmpeg child
                                                        │
                                                        ▼
                                               NUT container (.nut)
                                               (rawvideo, FFV1, or any codec)
```

---

## Files

| File | Role |
|------|------|
| `FFmpegRawOutput.cpp` | `extern "C"` entry point (`output_module_create` / `_destroy`) |
| `FFmpegRawModule.{h,cpp}` | `TwkApp::VideoModule` — exposes a single `FFmpegRaw` device |
| `FFmpegRawVideoDevice.{h,cpp}` | `TwkGLF::GLBindableVideoDevice` — does all the work |
| `CMakeLists.txt` | Builds `FFmpegRawOutput.so`, stages it as `OUTPUT_PLUGIN` |

No SDK dependency. `ffmpeg` is invoked at runtime; nothing is linked at build
time.

---

## Supported Formats

### Video resolutions

| Format | Frame rates |
|--------|-------------|
| 1920×1080 | 24, 29.97, 30, 60 Hz |
| 2048×1080 (DCI 2K) | 24, 29.97, 30, 48, 60 Hz |

### Pixel formats

| Name | Description |
|------|-------------|
| `8-bit RGBA` | Standard dynamic range capture (`GL_UNSIGNED_BYTE`) |
| `16-bit RGBA (HDR)` | High dynamic range capture (`GL_UNSIGNED_SHORT` → `rgba64le`) |

---

## Configuration (runtime env vars)

| Variable | Default | Purpose |
|----------|---------|---------|
| `RV_FFMPEG_NUT_OUTPUT` | `/tmp/rv_output.nut` | Output file path. Extension determines container if combined with `RV_FFMPEG_NUT_EXTRA_ARGS`. |
| `RV_FFMPEG_NUT_CODEC` | `rawvideo` | FFmpeg video codec. Use `ffv1` for lossless compression, `prores_ks` for editorial, etc. |
| `RV_FFMPEG_NUT_EXTRA_ARGS` | *(unset)* | Appended verbatim to the ffmpeg command for custom flags. |
| `RV_FFMPEG_BIN` | `ffmpeg` | Path to the ffmpeg binary. |

---

## Codec Examples

```bash
# Uncompressed raw (default) — largest files, zero encode overhead
export RV_FFMPEG_NUT_CODEC=rawvideo
export RV_FFMPEG_NUT_OUTPUT=/tmp/rv_capture.nut

# FFV1 lossless — ~3–5× smaller than raw, still lossless
export RV_FFMPEG_NUT_CODEC=ffv1
export RV_FFMPEG_NUT_OUTPUT=/tmp/rv_capture.nut

# ProRes 4444 for editorial (needs -f mov or mkv container via EXTRA_ARGS)
export RV_FFMPEG_NUT_CODEC=prores_ks
export RV_FFMPEG_NUT_EXTRA_ARGS="-profile:v 4444 -f mov"
export RV_FFMPEG_NUT_OUTPUT=/tmp/rv_capture.mov

# H.264 for lightweight review copies
export RV_FFMPEG_NUT_CODEC=libx264
export RV_FFMPEG_NUT_EXTRA_ARGS="-crf 18 -preset fast -f mp4"
export RV_FFMPEG_NUT_OUTPUT=/tmp/rv_capture.mp4

# HDR capture (select 16-bit RGBA in RV's device settings first)
export RV_FFMPEG_NUT_CODEC=ffv1
export RV_FFMPEG_NUT_OUTPUT=/tmp/rv_hdr_capture.nut
```

---

## Build

```bash
cmake --build /home/psawka/git/OpenRV/_build --target FFmpegRawOutput --parallel
```

Output: `_build/stage/app/plugins/Output/FFmpegRawOutput.so`

Launch RV:
```bash
/home/psawka/git/OpenRV/_build/stage/app/bin/rv
```

In Presentation Mode select `FFmpegRaw Output Module → FFmpegRaw`.

---

## Implementation Notes

- **10-frame ring buffer** (`kMaxQueueDepth = 10`) — larger than the streaming
  plugin's 3-frame buffer because disk I/O can hiccup longer than a network
  write. A warning is logged when a frame is dropped; this indicates the output
  storage is too slow for the chosen resolution and codec.

- **Background writer thread** — `transfer()` does only the `glReadPixels`
  readback and enqueues; the blocking `fwrite`/`fflush` to the pipe happens on
  a separate thread so RV's render loop is never stalled.

- **Vertical flip during memcpy** — GL reads rows bottom-up; the CPU-side flip
  during the copy to the queue slot corrects the orientation without a separate
  FFmpeg `-vf vflip` pass.

- **8 MB pipe buffer** via `fcntl(F_SETPIPE_SZ)` — reduces the number of
  syscalls for large frames (a 2K RGBA frame is ~8 MB).

- **`fork()` + `setsid()` + `execl()`** — ffmpeg child is detached from RV's
  process group so a terminal Ctrl-C doesn't terminate the capture.

- **SIGPIPE blocked** in the writer thread and around `fclose()` in `close()`.
  If ffmpeg exits early (e.g. disk full), the write returns an error rather than
  crashing RV with the default SIGPIPE handler.

- **`-y` flag** — ffmpeg overwrites the output file without prompting, so
  repeated captures to the same path work without intervention.

- **`-re` flag** — paces ffmpeg's pipe reads to the declared frame rate so
  timestamps are correct even when RV renders faster than real-time.

---

## Differences from RTP Output Plugin

| | FFmpegRaw | RTP Output |
|---|-----------|-----------|
| **Purpose** | Local file capture | Live network streaming |
| **Container** | NUT (or any via extra args) | MPEG-TS over RTP/SRT/UDP |
| **Audio** | None (video only) | OPUS (separate RTP stream) |
| **Ring buffer depth** | 10 frames | 3 frames |
| **Typical codec** | rawvideo / FFV1 | H.264 NVENC / HEVC NVENC |
| **Latency concern** | Disk throughput | Network latency |

---

## Verification Checklist

1. Build target compiles clean.
2. RV launches; `FFmpegRaw Output Module` appears in Presentation Mode.
3. Enable presentation; `pgrep -af ffmpeg` shows the spawned child writing to
   the output path.
4. Disable presentation → ffmpeg exits, `waitpid` reaps it cleanly.
5. Verify the output: `ffprobe /tmp/rv_output.nut`
6. Play it back: `ffplay /tmp/rv_output.nut`

---

## Known Limitations

- **No audio** — `transferAudio()` is a no-op; video-only capture.
- **NUT container by default** — widely supported by ffmpeg/ffplay but not
  by most NLEs directly; re-wrap with `ffmpeg -i input.nut -c copy output.mov`
  for editorial delivery.
- **Windows path** uses `_popen` — `F_SETPIPE_SZ` tuning and `fork`/`setsid`
  isolation are Linux/macOS only.
