
#
# Copyright (C) 2014 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Inherit from msm8974-common
-include device/oppo/msm8974-common/BoardConfigCommon.mk

# Kernel
BOARD_KERNEL_CMDLINE := console=ttyHSL0,115200,n8 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x3F ehci-hcd.park=3
TARGET_KERNEL_CONFIG := lineageos_n3_defconfig
TARGET_KERNEL_SOURCE := kernel/oppo/msm8974

# Assert
TARGET_OTA_ASSERT_DEVICE := n3,N3,N5206,N5207

# Audio
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
AUDIO_FEATURE_LOW_LATENCY_PRIMARY := true

# Bluetooth
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/oppo/n3/bluetooth

# Camera
BOARD_CAMERA_PLUGIN = device/oppo/n3/camera/plugin
BOARD_GLOBAL_CFLAGS += -DCAMERA_VENDOR_L_COMPAT
TARGET_HAS_LEGACY_CAMERA_HAL1 := true
TARGET_USES_MEDIA_EXTENSIONS := false
USE_DEVICE_SPECIFIC_CAMERA := true

# Filesystem
BOARD_BOOTIMAGE_PARTITION_SIZE     := 16777216
BOARD_CACHEIMAGE_PARTITION_SIZE    := 536870912
BOARD_PERSISTIMAGE_PARTITION_SIZE  := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 25165824
BOARD_SYSTEMIMAGE_PARTITION_SIZE   := 1577058304
BOARD_USERDATAIMAGE_PARTITION_SIZE := 114510381056 # 114510397440 - 16384 for crypto footer

# Init
TARGET_LIBINIT_MSM8974_DEFINES_FILE := device/oppo/n3/init/init_n3.cpp

# NFC
BOARD_NFC_CHIPSET := pn547
BOARD_NFC_HAL_SUFFIX := msm8974

# Properties
TARGET_SYSTEM_PROP += device/oppo/n3/system.prop

# Recovery
TARGET_RECOVERY_FSTAB := device/oppo/n3/rootdir/etc/fstab.recovery

# SELinux
BOARD_SEPOLICY_DIRS += device/oppo/n3/sepolicy

# Shims
TARGET_LD_SHIM_LIBS := \
    /system/vendor/lib/hw/camera.vendor.msm8974.so|libshim_camera.so \
    /system/lib/libcamera_client.so|libshim_camera_parameters.so

# TWRP
ifeq ($(WITH_TWRP),true)
TARGET_RECOVERY_DEVICE_DIRS += device/oppo/n3/twrp
endif

# Inherit from the proprietary version
include vendor/oppo/n3/BoardConfigVendor.mk
