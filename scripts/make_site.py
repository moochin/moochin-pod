#!/usr/bin/env python3
"""Builds the installer website (GitHub Pages) from a `pio run -e dial` build.

    python scripts/make_site.py <build dir> <output dir> <version>

Output: index.html (from installer/index.html), manifest.json for ESP Web
Tools, the flash parts, and two downloads:
  podhub-install.bin  whole-flash image for offset 0x0 (any web flasher)
  podhub-update.bin   app only, for "Update the hub" on the phone page
"""
import json
import os
import shutil
import sys


def main():
    build, out, version = sys.argv[1], sys.argv[2], sys.argv[3]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(out, exist_ok=True)

    boot_app0 = os.path.expanduser(
        "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")
    parts = [  # (file in the site, source, flash offset)
        ("bootloader.bin", os.path.join(build, "bootloader.bin"), 0x0),
        ("partitions.bin", os.path.join(build, "partitions.bin"), 0x8000),
        ("boot_app0.bin", boot_app0, 0xE000),
        ("firmware.bin", os.path.join(build, "firmware.bin"), 0x10000),
    ]
    for name, src, _ in parts:
        shutil.copyfile(src, os.path.join(out, name))
    shutil.copyfile(os.path.join(build, "podhub-factory.bin"), os.path.join(out, "podhub-install.bin"))
    shutil.copyfile(os.path.join(build, "firmware.bin"), os.path.join(out, "podhub-update.bin"))

    # Separate parts (not the merged image) so a reinstall over USB leaves the
    # NVS partition (pods, players, settings) alone unless "erase" is ticked.
    manifest = {
        "name": "Pod Hub",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": "ESP32-S3",
            "parts": [{"path": name, "offset": off} for name, _, off in parts],
        }],
    }
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    with open(os.path.join(root, "installer", "index.html")) as f:
        page = f.read().replace("{{VERSION}}", version)
    with open(os.path.join(out, "index.html"), "w") as f:
        f.write(page)
    print("Installer site for %s written to %s" % (version, out))


if __name__ == "__main__":
    main()
