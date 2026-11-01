# PlatformIO post-build step: gzip firmware.bin into firmware.bin.gz.
# Tasmota (and this firmware) accept gzipped images; the bootloader decompresses them.
import gzip
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)


def gzip_bin(source, target, env):
    src = str(target[0])
    dst = src + ".gz"
    with open(src, "rb") as f_in, gzip.open(dst, "wb", compresslevel=9) as f_out:
        shutil.copyfileobj(f_in, f_out)
    print(f"Compressed {src} -> {dst}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", gzip_bin)  # noqa: F821
