# Source-built Qualcomm Codec2 service entry points

The tested OS3.0.306.0.WNCCNXM service executables allocate 264 bytes for their
platform HIDL ComponentStore wrappers. On the current LineageOS branch the
audio wrapper alone needs 288 bytes. Using the current constructor with the
old allocation writes beyond the allocation, even when a short codec test
appears to work.

Build only the entry points from source, using the same platform headers and
libraries for the allocation and construction. Retain the vendor codec stores,
components, existing init/VINTF/SELinux entries, service instance names,
eight-thread HIDL pool, and original seccomp policy paths.

## Vendor factory ABI verification

Both old services call a factory getter with version (1, 0), then its third
vtable entry to obtain a shared_ptr<C2ComponentStore>. The getter only checks
the version and allocates an otherwise stateless factory. The third entry is
exactly the implementation of the exported QC2ComponentStore::Get(). Calling
that export directly avoids reconstructing an undocumented factory class.

Evidence from the tested unstripped exports and disassembly:

| Service | Allocation instruction | Store constructor | Factory entry / exported Get implementation |
| --- | --- | --- | --- |
| Audio | 0x60b4: mov w0, #0x108 | HIDL 1.0 | libqc2audio_core.so: vtable 0x4f7b0 + 0x10 → 0x42240; Get export 0x4d210 → 0x42240 |
| Video | 0x29c8: mov w0, #0x108 | HIDL 1.2 | libqcodec2_core.so: vtable 0x5e858 + 0x10 → 0x497f4; Get export 0x5ad00 → 0x497f4 |

The services use explicit symbol aliases for these exports and retain the
appropriate HIDL versions (audio 1.0/default2, video 1.2/default).
No CFI or SELinux check is disabled.

## Validation requirements

Verify both component stores register with the expected instance names and run
in the mediacodec SELinux domain with Seccomp: 2. Decode synthetic AAC/H.264
streams through output EOS and encode synthetic PCM/YUV input through EOS;
repeat across service restarts and full system boots. Check the boot crash
buffer, especially mediacodeclist_generator/default2, independently of runtime
codec success. Other firmware baselines need their own ABI and device testing.

## Tested result on houji

Both source-built wrappers report 288 bytes. After installing the full ROM,
AAC and AVC decoding each completed three EOS runs; AAC and AVC encoding each
completed three 60-frame EOS runs. Both services retained the mediacodec domain,
NoNewPrivs: 1 and Seccomp: 2. Two consecutive system boots had empty crash
buffers, including no mediacodeclist_generator/default2 crash. These checks
cover the tested synthetic streams, not every codec, profile or resolution.
