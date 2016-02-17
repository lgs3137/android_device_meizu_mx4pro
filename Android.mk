LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),mx4pro)

include $(CLEAR_VARS)

ALL_PREBUILT += $(INSTALLED_KERNEL_TARGET)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif
