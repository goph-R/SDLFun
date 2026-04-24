#!/usr/bin/env python3
"""One-shot migration: convert every BMP/TGA under assets/ to PNG.

Writes the PNG next to the original (same stem, .png extension). The
alpha channel is preserved only when the source really carries meaningful
transparency -- a uniformly-opaque or uniformly-zero alpha is treated as
padding and flattened to RGB, because some paint tools write garbage into
the alpha byte of 32-bit BMPs and blindly preserving it produces a fully
transparent PNG.

Usage:
    python tools/bmp_tga_to_png.py              # convert, keep originals
    python tools/bmp_tga_to_png.py --delete     # convert then rm originals
    python tools/bmp_tga_to_png.py --dry-run    # print actions only
"""

import argparse
import pathlib
import sys

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("error: Pillow is required (pip install Pillow)\n")
    sys.exit(1)


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSET_ROOT = REPO_ROOT / "assets"
SRC_EXTS = {".bmp", ".tga"}


def alpha_is_meaningful(img):
    """True if the alpha channel carries real data, not padding."""
    if img.mode != "RGBA":
        return False
    alpha = img.getchannel("A")
    lo, hi = alpha.getextrema()
    # Constant alpha (whether 0, 255, or anything in between) => padding.
    if lo == hi:
        return False
    # All pixels near-opaque (alpha >= 250) => also treat as padding noise.
    if lo >= 250:
        return False
    return True


def convert_one(src, dry_run):
    dst = src.with_suffix(".png")
    img = Image.open(src)

    if img.mode == "P":
        img = img.convert("RGBA" if "transparency" in img.info else "RGB")
    elif img.mode not in ("RGB", "RGBA"):
        img = img.convert("RGBA" if "A" in img.mode else "RGB")

    if img.mode == "RGBA" and not alpha_is_meaningful(img):
        img = img.convert("RGB")

    mode = img.mode
    action = "would write" if dry_run else "write"
    print(f"{action} {dst.relative_to(REPO_ROOT)}  ({mode}, {img.size[0]}x{img.size[1]})")

    if not dry_run:
        img.save(dst, format="PNG", optimize=True)

    return dst


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--delete", action="store_true", help="remove source BMP/TGA after successful convert")
    ap.add_argument("--dry-run", action="store_true", help="print actions without writing")
    args = ap.parse_args()

    if not ASSET_ROOT.is_dir():
        sys.stderr.write(f"error: {ASSET_ROOT} not found\n")
        sys.exit(1)

    sources = sorted(
        p for p in ASSET_ROOT.rglob("*")
        if p.is_file() and p.suffix.lower() in SRC_EXTS
    )

    if not sources:
        print("no BMP/TGA files found under assets/")
        return

    print(f"found {len(sources)} source file(s)\n")
    converted = []
    for src in sources:
        try:
            dst = convert_one(src, args.dry_run)
            converted.append((src, dst))
        except Exception as exc:
            sys.stderr.write(f"error: failed to convert {src}: {exc}\n")
            sys.exit(2)

    if args.delete and not args.dry_run:
        print()
        for src, _ in converted:
            print(f"rm {src.relative_to(REPO_ROOT)}")
            src.unlink()


if __name__ == "__main__":
    main()
