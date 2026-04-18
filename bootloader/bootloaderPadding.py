BOOTLOADER_SIZE = 0x8000
BOOTLOADER_FILE = "bootloader.bin"

with open(BOOTLOADER_FILE, "rb") as f:
    raw_file = f.read()

pad_size = BOOTLOADER_SIZE - len(raw_file)
padding = bytes(0xff for _ in range(pad_size))

with open("bootloader.bin", "wb") as f:
    f.write(raw_file + padding)

    