# Extracted with libbootimg
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x26000000 --tags_offset 0x00000100 --dt device/meizu/mx4pro/dtb.img

# Kernel
TARGET_KERNEL_CONFIG := mx4pro_defconfig

# Recovery
TARGET_OTA_ASSERT_DEVICE := mx4pro,m76

# Inherit common board flags
include device/meizu/mx4pro/BoardConfigCommon.mk
