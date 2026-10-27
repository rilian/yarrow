---
id: YAR-031
title: The firmware's CJK fonts are fetched from a moving branch and rendered with an unpinned Pillow, so a rebuild can change the font data
type: bug
status: confirmed
severity: low
platforms: [mcu]
area: build
next: Fetch the Noto CJK fonts from a tag or commit with a SHA-256 in third_party/noto-cjk/README.md; pin Pillow in a requirements file
pattern: PAT-029
aliases: []
reported: 2026-10-27
source: pattern check 2026-10-27
where: third_party/noto-cjk/README.md:7-10; docs/building.md:7-8; tools/genfont.py:9, 30-32, 103-104 @ 78b1446
fixed_in:
---

## Symptom

None reported. Two builds of the same commit can produce different `font_data.c`, and so different firmware images,
for every language, and in particular for `zh`, `ja` and `ko`.

## Cause

- The Noto Sans CJK files are not in the repository; third_party/noto-cjk/README.md:7-10 tells the builder to fetch them
  with `curl -LO` from the `main` branch of the upstream noto-cjk repository, with no tag or checksum.
  `tools/genfont.py` renders the glyphs the Chinese, Japanese and Korean texts use from them (30-32, 104) into
  `font_data.c`, which is linked into the released firmware.
- `tools/genfont.py` renders every language's glyphs with Pillow (`ImageFont.truetype`, 9, 103); docs/building.md:7-8
  installs it as `pip install pillow numpy unicorn` without versions. Pillow ships its own FreeType, whose rendering and
  hinting can change between releases.
- The Inter fonts are vendored (third_party/inter). The toolchains come from the build machine: `arm-none-eabi-gcc` 12
  or newer (docs/building.md:5), Rust 1.88 or newer for the hub (no `rust-toolchain` file). The hub's Rust and npm
  dependencies are pinned by the committed `hub/Cargo.lock` and `hub/app/ui/package-lock.json`.

Verified by reading at 78b1446. Whether a changed font release or Pillow version changes the glyph bitmaps was not
measured.

## Fix

Fetch the CJK fonts from a release tag or a commit, and check a SHA-256 in the README (or in a small fetch script);
pin Pillow (a `requirements.txt` with exact versions); print the font and Pillow versions into the build log. Document
the toolchain versions used for a release.

## Test

Build `make YLANG=ja` twice in clean environments a week apart and compare `build/ja/font_data.c`; or `sha256sum` the
fetched font files against the recorded values.

## Log

- 2026-10-27: found while checking PAT-029 at 78b1446.
