# PlatformIO post-build step: merge bootloader + partitions + app into one
# image flashed at offset 0x0. This is what the browser installer flashes.
Import("env")  # noqa: F821  (provided by PlatformIO)

import os


def merge(source, target, env):
    build = env.subst("$BUILD_DIR")
    out = os.path.join(build, "podhub-factory.bin")
    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))  # [addr, path, addr, path...]
    app = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    args = []
    for i in range(0, len(flash_images), 2):
        args += [flash_images[i], env.subst(flash_images[i + 1])]
    args += [env.subst("$ESP32_APP_OFFSET"), app]
    cmd = " ".join(
        [
            '"$PYTHONEXE"',
            '"$OBJCOPY"',
            "--chip", env.BoardConfig().get("build.mcu"),
            "merge_bin",
            "--flash_mode", "dio",
            "--flash_size", env.BoardConfig().get("upload.flash_size", "8MB"),
            "-o", '"%s"' % out,
        ]
        + ['"%s"' % a for a in args]
    )
    env.Execute(cmd)
    print("Merged image: %s" % out)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
