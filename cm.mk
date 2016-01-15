# Release name
PRODUCT_RELEASE_NAME := mx4pro

# Inherit from the common Open Source product configuration
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base_telephony.mk)

# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_phone.mk)

# Inherit nfc enhanced configuration
$(call inherit-product, vendor/cm/config/nfc_enhanced.mk)

# Inherit device configuration
$(call inherit-product, device/meizu/mx4pro/device_mx4pro.mk)

# Boot animation
TARGET_SCREEN_HEIGHT := 2560
TARGET_SCREEN_WIDTH := 1536

## Device identifier. This must come after all inclusions
PRODUCT_NAME := cm_mx4pro
PRODUCT_DEVICE := mx4pro
PRODUCT_BRAND := meizu
PRODUCT_MODEL := MX4 Pro
PRODUCT_MANUFACTURER := meizu

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=mx4pro
