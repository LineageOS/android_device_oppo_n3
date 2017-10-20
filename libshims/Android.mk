 
LOCAL_PATH := $(call my-dir)

# Camera

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    CameraParameters.cpp

LOCAL_MODULE := libshim_camera_parameters
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_TAGS := optional

LOCAL_32_BIT_ONLY := true
include $(BUILD_SHARED_LIBRARY)