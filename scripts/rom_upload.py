"""Keep VoiceS3R native USB stable by uploading through the ESP32-S3 ROM.

This unit re-enumerates or stops responding when esptool launches its optional
RAM flasher stub. PlatformIO normally adds compression and uses that stub.
Removing compression and adding --no-stub keeps the same standard PlatformIO
upload target while using Espressif's ROM write implementation instead.

The native USB reset lines can also make this board sample download mode again
after a successful write. Ask esptool to leave the ROM loader through its
software-run path instead of issuing the usual hard reset.
"""

Import("env")

flags = list(env.get("UPLOADERFLAGS", []))
if "write_flash" in flags:
    flags = [flag for flag in flags if str(flag) != "-z"]
    command_index = flags.index("write_flash")
    if "--no-stub" not in flags:
        flags.insert(command_index, "--no-stub")

    if "--after" in flags:
        after_index = flags.index("--after")
        if after_index + 1 < len(flags):
            flags[after_index + 1] = "soft_reset"
    else:
        flags[0:0] = ["--after", "soft_reset"]

    env.Replace(UPLOADERFLAGS=flags)
