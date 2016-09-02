LOCAL_PATH := device/meizu/mx4pro

TARGET_ARCH := arm
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Platform
TARGET_BOARD_PLATFORM := exynos5
TARGET_SLSI_VARIANT := cm
TARGET_SOC := exynos5430

# CPU
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a15
TARGET_CPU_SMP := true
ARCH_ARM_HAVE_TLS_REGISTER := true

# RENDERSCRIPT
BOARD_OVERRIDE_RS_CPU_VARIANT_32 := cortex-a15

# Enable dex-preoptimization to speed up first boot sequence
ifeq ($(HOST_OS),linux)
  ifeq ($(TARGET_BUILD_VARIANT),user)
    ifeq ($(WITH_DEXPREOPT),)
      WITH_DEXPREOPT := true
    endif
  endif
endif
WITH_DEXPREOPT_BOOT_IMG_ONLY ?= true

# BOOT
TARGET_BOOTLOADER_BOARD_NAME := m76

BOARD_KERNEL_BASE := 0x26000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_CMDLINE := console=ttyFIQ2,115200 vmalloc=512M ess_setup=0x26000000 ramoops_setup reset_reason_setup noexec=on earlyprintk no_console_suspend

BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 16777216
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1610612736
BOARD_USERDATAIMAGE_PARTITION_SIZE := 28219277312
BOARD_FLASH_BLOCK_SIZE := 131072


# Sensors
TARGET_NO_SENSOR_PERMISSION_CHECK := true

# Kernel
TARGET_KERNEL_SOURCE := kernel/meizu/mx4pro

# Use these flags if the board has a ext4 partition larger than 2gb
BOARD_HAS_LARGE_FILESYSTEM := true
TARGET_USERIMAGES_USE_EXT4 := true

# Extended filesystem support
TARGET_KERNEL_HAVE_EXFAT := true
TARGET_KERNEL_HAVE_NTFS := true

# Camera
BOARD_USE_SAMSUNG_CAMERAFORMAT_NV21 := true

# Graphics
USE_OPENGL_RENDERER := true

# Shader cache config options
# Maximum size of the  GLES Shaders that can be cached for reuse.
# Increase the size if shaders of size greater than 12KB are used.
MAX_EGL_CACHE_KEY_SIZE := 12*1024

# Maximum GLES shader cache size for each app to store the compiled shader
# binaries. Decrease the size if RAM or Flash Storage size is a limitation
# of the device.
MAX_EGL_CACHE_SIZE := 2048*1024
# frameworks/native/services/surfaceflinger
# Android keeps 2 surface buffers at all time in case the hwcomposer
# misses the time to swap buffers (in cases where it takes 16ms or
# less). Use 3 to avoid timing issues.
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3

# Mixer
BOARD_USE_BGRA_8888 := true

# HWCServices
BOARD_USES_HWC_SERVICES := true

# Virtual Display
BOARD_USES_VIRTUAL_DISPLAY := true

# HDMI
TARGET_LINUX_KERNEL_VERSION := 3.10
BOARD_USES_GSC_VIDEO := true
BOARD_USES_CEC := true

# FIMG2D
BOARD_USES_SKIA_FIMGAPI := true

# (G)SCALER
BOARD_USES_SCALER := true
BOARD_USES_DT := true

# Keymaster
BOARD_USES_TRUST_KEYMASTER := true

# Samsung LSI OpenMAX
COMMON_GLOBAL_CFLAGS += -DUSE_NATIVE_SEC_NV12TILED

# Samsung Seiren audio
BOARD_USE_ALP_AUDIO := true
BOARD_USE_SEIREN_AUDIO := true

# Samsung OpenMAX Video
BOARD_USE_STOREMETADATA := true
BOARD_USE_METADATABUFFERTYPE := true
BOARD_USE_DMA_BUF := true
BOARD_USE_ANB_OUTBUF_SHARE := true
BOARD_USE_IMPROVED_BUFFER := true
BOARD_USE_NON_CACHED_GRAPHICBUFFER := true
BOARD_USE_GSC_RGB_ENCODER := true
BOARD_USE_CSC_HW := false
BOARD_USE_QOS_CTRL := false
BOARD_USE_S3D_SUPPORT := true
BOARD_USE_VP8ENC_SUPPORT := true

# Include path
TARGET_SPECIFIC_HEADER_PATH := $(LOCAL_PATH)/include

# WIFI
BOARD_WPA_SUPPLICANT_DRIVER	:= NL80211
WPA_SUPPLICANT_VERSION		:= VER_0_8_X
BOARD_WPA_SUPPLICANT_PRIVATE_LIB	:= lib_driver_cmd_bcmdhd
BOARD_HOSTAPD_DRIVER		:= NL80211
BOARD_HOSTAPD_PRIVATE_LIB	:= lib_driver_cmd_bcmdhd
BOARD_WLAN_DEVICE		:= bcmdhd
WIFI_DRIVER_FW_PATH_PARAM	:= "/sys/module/bcmdhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_AP		:= "/vendor/firmware/fw_bcmdhd_apsta.bin"
WIFI_DRIVER_FW_PATH_STA		:= "/vendor/firmware/fw_bcmdhd.bin"

# BLUETOOTH
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_BLUEDROID_VENDOR_CONF := $(LOCAL_PATH)/bluetooth/vnd_mx4pro.txt
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(LOCAL_PATH)/bluetooth

### NFC
BOARD_NFC_CHIPSET := PN547C2
BOARD_NFC_HAL_SUFFIX := $(TARGET_BOOTLOADER_BOARD_NAME)

# Webkit
ENABLE_WEBGL := true

# WFD
BOARD_USES_WFD := true

### TWRP RECOVERY
RECOVERY_VARIANT := twrp
TW_EXTRA_LANGUAGES := true
TW_DEFAULT_LANGUAGE := zh_CN
TARGET_RECOVERY_FSTAB := $(LOCAL_PATH)/configs/script/recovery.fstab
TW_THEME := portrait_hdpi
TW_MTP_DEVICE := "/dev/mtp_usb"
TW_ALWAYS_RMRF := true
TW_HAS_NO_BOOT_PARTITION := true
TW_HAS_NO_RECOVERY_PARTITION := true
BOARD_HAS_NO_REAL_SDCARD := true
RECOVERY_SDCARD_ON_DATA := true
TW_NO_REBOOT_BOOTLOADER := true
TW_NO_EXFAT_FUSE := true
TW_NO_EXFAT := true

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := $(LOCAL_PATH)/configs/script
