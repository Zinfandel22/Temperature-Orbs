import os

# Tell the IDE linter to ignore PlatformIO's SCons injected variables
Import("env")  # type: ignore

# Paths to the compiled binaries
APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"
MERGED_BIN = "$BUILD_DIR/${PROGNAME}_merged.bin"
FS_BIN = "$BUILD_DIR/littlefs.bin"
DUMMY_BIN = "$BUILD_DIR/dummy_pad.bin"

# Filesystem offset exactly as defined in your partitions.csv
FS_OFFSET = "0x210000"

BOARD_CONFIG = env.BoardConfig()  # type: ignore

def merge_bin(source, target, env):
    print("\n--- Starting Firmware Merge Process ---")
    
    # 0. Force the file to start at 0x0 by creating a blank 4KB dummy file
    dummy_path = env.subst(DUMMY_BIN)
    with open(dummy_path, "wb") as f:
        # Write 4096 bytes of 0xFF (standard erased flash state)
        f.write(b'\xff' * 4096)
        
    # Start the image list at 0x0 with our padding file
    flash_images = ["0x0", DUMMY_BIN]
    
    # 1. Grab the bootloader, partitions, and boot_app0
    flash_images += env.Flatten(env.get("FLASH_EXTRA_IMAGES", []))
    
    # 2. Add the main application firmware
    app_offset = env.get("ESP32_APP_OFFSET", "0x10000")
    flash_images += [app_offset, APP_BIN]
    
    # 3. Add the LittleFS filesystem binary if it has been built
    fs_path = env.subst(FS_BIN)
    if os.path.exists(fs_path):
        print(f"Found LittleFS image. Merging at {FS_OFFSET}...")
        flash_images += [FS_OFFSET, FS_BIN]
    else:
        print("No LittleFS image found. Skipping FS merge.")

    # 4. Construct and execute the esptool merge_bin command
    cmd = [
        "$PYTHONEXE", "$OBJCOPY", 
        "--chip", BOARD_CONFIG.get("build.mcu", "esp32"), 
        "merge_bin", 
        "--fill-flash-size", BOARD_CONFIG.get("upload.flash_size", "4MB"), 
        "-o", MERGED_BIN
    ] + flash_images
    
    # Execute the merge command
    env.Execute(" ".join(cmd))
    
    merged_path = env.subst(MERGED_BIN)
    print("--- Merge Complete! ---")
    print(f"Your fully combined .bin is located at: {merged_path}\n")

# Attach this to the post-build action
env.AddPostAction(APP_BIN, merge_bin)  # type: ignore