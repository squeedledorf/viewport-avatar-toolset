# Security policy

## Supported versions

Security fixes go into the latest release. Older releases are not patched; update to the newest one.

## Reporting a vulnerability

Please report security problems privately, not in a public issue: use **Security → Report a
vulnerability** on the repository's GitHub page (GitHub private vulnerability reporting). Include the
VATs version, your operating system, what an attacker could do, and a file or steps that reproduce it.

You should get a reply within two weeks. Once a fix is released, the report is credited in the release
notes unless you ask otherwise.

## What counts

VATs opens files that other people make, so bugs in its readers matter most:

- project files (`.vat`), pose and prop libraries, and settings (JSON);
- Second Life `.anim`, BVH, COLLADA (`.dae`), FBX (through the vendored ufbx) and glTF/GLB;
- audio files (WAV, MP3, FLAC and Ogg Vorbis, through the vendored dr_libs and stb_vorbis);
- the motion-capture and face-tracking network listeners, which accept UDP packets while they are
  switched on (Tools → Motion Capture...).

A crash, hang, out-of-bounds read or write, or excessive memory use caused by a crafted file or packet is
a security bug. Problems in the vendored libraries are welcome too; they are also reported upstream.
