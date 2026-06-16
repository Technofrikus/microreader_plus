Import("env")
import os
import shutil

FIRMWARE_META_MAGIC = b"MRDRFW__"


def copy_firmware(source, target, env):
    src = str(target[0])
    dst = os.path.join(env.get("PROJECT_DIR"), "firmware.bin")

    with open(src, "rb") as f:
        data = f.read()
    if FIRMWARE_META_MAGIC not in data:
        raise RuntimeError(
            "[copy_firmware] FirmwareMeta marker '%s' not found in %s; "
            "the marker was stripped (gc-sections). Add a live reference to "
            "g_firmware_meta so the symbol is retained."
            % (FIRMWARE_META_MAGIC.decode("ascii"), src)
        )

    shutil.copy(src, dst)
    print(f"[copy_firmware] firmware.bin -> {dst}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_firmware)
