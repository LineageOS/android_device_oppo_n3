# Copyright (C) 2014-2016 The CyanogenMod Project
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

# Inherit framework first
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from n3 device
$(call inherit-product, device/oppo/n3/n3.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_NAME := lineage_n3
PRODUCT_DEVICE := n3
PRODUCT_BRAND := OPPO
PRODUCT_MANUFACTURER := OPPO
PRODUCT_MODEL := N3

PRODUCT_GMS_CLIENTID_BASE := android-oppo

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="msm8974-user 4.4.4 KTU84P eng.root.20150907.162651 dev-keys" \
    TARGET_DEVICE="N3"

BUILD_FINGERPRINT := OPPO/N5206/N3:4.4.4/KTU84P/1415366188:user/release-keys
