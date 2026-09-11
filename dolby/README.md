# Dolby Codec2 service wrapper

The CN houji prebuilt service allocates 264 bytes for the HIDL 1.0 ComponentStore,
but the current platform wrapper requires 288 bytes. Its constructor initializes
RefBase at offset 272, beyond the prebuilt allocation. Build the small service
entry point with current platform headers to avoid this ABI mismatch.

The Dolby component-store factory and decoder implementations remain proprietary.
The service keeps the original binary path, `default1` registration, vendor binder,
eight HIDL threads and existing init/VINTF/SELinux configuration. Regenerate vendor
makefiles after updating the proprietary list to remove the old executable module.

Two related fixes are required: the FilterWrapper shim must return an empty
`std::shared_ptr<C2ParamReflector>`, and the active `_pineapple` codec XML must include
the Dolby audio codec list. An empty void shim leaves return storage uninitialized;
an omitted codec include makes MediaCodec reject the decoder with NAME_NOT_FOUND.

## Reproduction and validation

Tested on a 23127PN0CC houji with OS3.0.306.0.WNCCNXM blobs and LineageOS 24.0.
Temporary tests passed in SELinux Enforcing:

- Native shim ABI probe: old shim exits 1, repaired shim exits 0.
- E-AC3 create/configure/start/stop: three successful repetitions after repairs.
- Actual E-AC3 decode: three runs to output EOS, 379,904 PCM bytes per run,
  with nonzero PCM data, at 48 kHz stereo. No new fatal crash in that test window.

The ROM was subsequently built, installed and booted. Partition hashes matched
the tested binaries/configuration with no temporary mounts. All three checks
passed again after reboot; the captured crash buffer was empty. SELinux remained Enforcing.

Generate a synthetic test file without using private media:

```sh
ffmpeg -f lavfi -i sine=frequency=1000:sample_rate=48000:duration=2 \
    -ac 2 -c:a eac3 -b:a 192k test-eac3.mp4
```

Compile `tests/DecodeProbe.java` against an Android SDK android.jar and convert
the class with d8. Package classes.dex as decode-probe.jar. With an authorized
debugging connection, push the jar and generated MP4 to /data/local/tmp, then run:

```sh
adb shell 'CLASSPATH=/data/local/tmp/decode-probe.jar app_process /system/bin DecodeProbe /data/local/tmp/test-eac3.mp4'
```

`tests/shim-probe.c` is an AArch64 ABI probe that poisons the hidden shared_ptr
return storage and checks both words and adjacent guards after calling the shim.
Build it as a freestanding Android PIE, entry `_start`, linked with libdl/libc.
Run with `LD_LIBRARY_PATH=/vendor/lib64`; exit 2 means symbol/library loading failed.

These checks do not certify Dolby Atmos effects, Dolby Vision, DRM playback,
all channel layouts, sound output quality or long-duration playback.
