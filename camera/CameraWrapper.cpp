/*
 * Copyright (C) 2014, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file CameraWrapper.cpp
*
* This file wraps a vendor camera module.
*
*/

//#define LOG_NDEBUG 0

#define LOG_TAG "CameraWrapper"
#include <cutils/log.h>

#include <utils/AndroidThreads.h>
#include <utils/String8.h>
#include <hardware/hardware.h>
#include <hardware/camera.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define UNUSED __attribute__((unused))

static android::Mutex gCameraWrapperLock;
static camera_module_t *gVendorModule = 0;

static android::Mutex gRotateLock;
static android_thread_id_t gRotateThreadId = 0;
static bool gRotateToFfc;
static int gRotateSpeed;
static long long gRotateTimeout = 0;

static const char * MOTOR_ANGLE_FILE = "/sys/class/motor/cameramotor/mdangel";
static const char * MOTOR_DIRECTION_FILE = "/sys/class/motor/cameramotor/mddir";
static const char * MOTOR_SPEED_FILE = "/sys/class/motor/cameramotor/mdspeed";
static const char * MOTOR_MODE_FILE = "/sys/class/motor/cameramotor/mdmode";
static const char * MOTOR_ENABLE_FILE = "/sys/class/motor/cameramotor/pwm_enable";
static const char * MOTOR_BLOCK_DETECT_FILE = "/proc/cam_block";

static char *fixed_set_params = NULL;

static int camera_device_open(const hw_module_t *module, const char *name,
        hw_device_t **device);
static int camera_get_number_of_cameras(void);
static int camera_get_camera_info(int camera_id, struct camera_info *info);

static struct hw_module_methods_t camera_module_methods = {
    .open = camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = CAMERA_MODULE_API_VERSION_1_0,
         .hal_api_version = HARDWARE_HAL_API_VERSION,
         .id = CAMERA_HARDWARE_MODULE_ID,
         .name = "N3 Camera Wrapper",
         .author = "The CyanogenMod Project",
         .methods = &camera_module_methods,
         .dso = NULL, /* remove compilation warnings */
         .reserved = {0}, /* remove compilation warnings */
    },
    .get_number_of_cameras = camera_get_number_of_cameras,
    .get_camera_info = camera_get_camera_info,
    .set_callbacks = NULL, /* remove compilation warnings */
    .get_vendor_tag_ops = NULL, /* remove compilation warnings */
    .open_legacy = NULL, /* remove compilation warnings */
    .set_torch_mode = NULL, /* remove compilation warnings */
    .init = NULL, /* remove compilation warnings */
    .reserved = {0}, /* remove compilation warnings */
};

typedef struct wrapper_camera_device {
    camera_device_t base;
    int id;
    camera_device_t *vendor;
} wrapper_camera_device_t;

static camera_device_t *gVendorDeviceHandle = NULL;
static unsigned int gUseFlags = 0;

#define VENDOR_CALL(device, func, ...) ({ \
    wrapper_camera_device_t *__wrapper_dev = (wrapper_camera_device_t*) device; \
    __wrapper_dev->vendor->ops->func(__wrapper_dev->vendor, ##__VA_ARGS__); \
})

#define CAMERA_ID(device) (((wrapper_camera_device_t *)(device))->id)

static int check_vendor_module()
{
    int rv = 0;
    ALOGV("%s", __FUNCTION__);

    if (gVendorModule)
        return 0;

    rv = hw_get_module_by_class("camera", "vendor",
            (const hw_module_t**)&gVendorModule);
    if (rv)
        ALOGE("failed to open vendor camera module");
    return rv;
}

static void write_int(const char *file, int value)
{
    int fd = open(file, O_WRONLY);
    if (fd >= 0) {
        char buf[10];
        snprintf(buf, sizeof(buf), "%d", value);
        if (write(fd, buf, strlen(buf)) < 0) {
            ALOGE("Write to %s failed: %d", file, errno);
        }
        close(fd);
    } else {
        ALOGE("Open %s failed: %d", file, errno);
    }
}

static long long get_current_time_millis()
{
    struct timespec t;
    int r = clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int rotate_camera_thread(void *)
{
    while (true) {
        usleep(50 * 1000);

        android::Mutex::Autolock lock(gRotateLock);
        if (get_current_time_millis() >= gRotateTimeout) {
            ALOGD("Rotate camera to %s", gRotateToFfc ? "FFC" : "BFC");

            write_int(MOTOR_ENABLE_FILE, 0);
            write_int(MOTOR_ANGLE_FILE, 215);
            write_int(MOTOR_DIRECTION_FILE, gRotateToFfc ? 1 : 0);
            write_int(MOTOR_SPEED_FILE, gRotateSpeed);
            write_int(MOTOR_MODE_FILE, 6);
            write_int(MOTOR_BLOCK_DETECT_FILE, gRotateSpeed);
            write_int(MOTOR_ENABLE_FILE, 1);

            gRotateThreadId = 0;
            break;
        }
    }

    return 0;
}

static void rotate_camera(bool to_ffc, int speed, unsigned int timeout)
{
    android::Mutex::Autolock lock(gRotateLock);

    ALOGI("Camera rotate to %s triggered", to_ffc ? "FFC" : "BFC");
    if (gRotateThreadId == 0) {
        androidCreateThreadEtc(rotate_camera_thread, NULL,
                "CameraMotorDelay", android::PRIORITY_DEFAULT, 0, &gRotateThreadId);
    }

    gRotateToFfc = to_ffc;
    gRotateSpeed = speed;
    gRotateTimeout = get_current_time_millis() + timeout;
}

static const char *KEY_EXPOSURE_TIME = "exposure-time";
static const char *KEY_EXPOSURE_TIME_VALUES = "exposure-time-values";

static char *camera_fixup_getparams(UNUSED int id, const char *settings)
{
    bool videoMode = false;
    const char *exposureTimeValues = "0,1,500000,1000000,2000000,4000000,8000000,16000000,32000000,64000000";

    android::CameraParameters params;
    params.unflatten(android::String8(settings));

#if !LOG_NDEBUG
    ALOGV("%s: original parameters:", __FUNCTION__);
    params.dump();
#endif

    if (params.get(android::CameraParameters::KEY_RECORDING_HINT)) {
        videoMode = (!strcmp(params.get(
                android::CameraParameters::KEY_RECORDING_HINT), "true"));
    }

    /* Remove unsupported features */
    params.remove("af-bracket");
    params.remove("af-bracket-values");
    params.remove("chroma-flash");
    params.remove("chroma-flash-values");
    params.remove("dis");
    params.remove("dis-values");
    params.remove("opti-zoom");
    params.remove("opti-zoom-values");
    params.remove("see-more");
    params.remove("see-more-values");
    params.remove("still-more");
    params.remove("still-more-values");

    if (!videoMode) {
        /* Set supported exposure time values */
        params.set(KEY_EXPOSURE_TIME_VALUES, exposureTimeValues);
    }

#if !LOG_NDEBUG
    ALOGV("%s: fixed parameters:", __FUNCTION__);
    params.dump();
#endif

    android::String8 strParams = params.flatten();
    char *ret = strdup(strParams.string());

    return ret;
}

static char *camera_fixup_setparams(UNUSED int id, const char *settings)
{
    bool videoMode = false;
    bool slowShutterMode = false;

    android::CameraParameters params;
    params.unflatten(android::String8(settings));

#if !LOG_NDEBUG
    ALOGV("%s: original parameters:", __FUNCTION__);
    params.dump();
#endif

    if (params.get(android::CameraParameters::KEY_RECORDING_HINT)) {
        videoMode = (!strcmp(params.get(
                android::CameraParameters::KEY_RECORDING_HINT), "true"));
    }

    if (params.get(KEY_EXPOSURE_TIME)) {
        slowShutterMode = (strcmp(params.get(KEY_EXPOSURE_TIME), "0"));
    }

    /* Disable flash if slow shutter is enabled */
    if (!videoMode) {
        if (slowShutterMode) {
            params.set(android::CameraParameters::KEY_FLASH_MODE,
                    android::CameraParameters::FLASH_MODE_OFF);
        }
    }

#if !LOG_NDEBUG
    ALOGV("%s: fixed parameters:", __FUNCTION__);
    params.dump();
#endif

    android::String8 strParams = params.flatten();
    if (fixed_set_params)
        free(fixed_set_params);
    fixed_set_params = strdup(strParams.string());

    return fixed_set_params;
}

/*******************************************************************
 * implementation of camera_device_ops functions
 *******************************************************************/

static int camera_set_preview_window(struct camera_device *device,
        struct preview_stream_ops *window)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, set_preview_window, window);
}

static void camera_set_callbacks(struct camera_device *device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, set_callbacks, notify_cb, data_cb, data_cb_timestamp,
            get_memory, user);
}

static void camera_enable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, enable_msg_type, msg_type);
}

static void camera_disable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, disable_msg_type, msg_type);
}

static int camera_msg_type_enabled(struct camera_device *device,
        int32_t msg_type)
{
    if (!device)
        return 0;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, msg_type_enabled, msg_type);
}

static int camera_start_preview(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, start_preview);
}

static void camera_stop_preview(struct camera_device *device)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, stop_preview);
}

static int camera_preview_enabled(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, preview_enabled);
}

static int camera_store_meta_data_in_buffers(struct camera_device *device,
        int enable)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, store_meta_data_in_buffers, enable);
}

static int camera_start_recording(struct camera_device *device)
{
    if (!device)
        return EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, start_recording);
}

static void camera_stop_recording(struct camera_device *device)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, stop_recording);
}

static int camera_recording_enabled(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, recording_enabled);
}

static void camera_release_recording_frame(struct camera_device *device,
        const void *opaque)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, release_recording_frame, opaque);
}

static int camera_auto_focus(struct camera_device *device)
{
    if (!device)
        return -EINVAL;


    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, auto_focus);
}

static int camera_cancel_auto_focus(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, cancel_auto_focus);
}

static int camera_take_picture(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, take_picture);

static int camera_cancel_picture(struct camera_device *device)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, cancel_picture);
}

static int camera_set_parameters(struct camera_device *device,
        const char *params)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    char *tmp = NULL;
    tmp = camera_fixup_setparams(CAMERA_ID(device), params);

    int ret = VENDOR_CALL(device, set_parameters, tmp);
    return ret;
}

static char *camera_get_parameters(struct camera_device *device)
{
    if (!device)
        return NULL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    char *params = VENDOR_CALL(device, get_parameters);

    char *tmp = camera_fixup_getparams(CAMERA_ID(device), params);
    VENDOR_CALL(device, put_parameters, params);
    params = tmp;

    return params;
}

static void camera_put_parameters(struct camera_device *device, char *params)
{
    if (params)
        free(params);

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));
}

static int camera_send_command(struct camera_device *device,
        int32_t cmd, int32_t arg1, int32_t arg2)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (cmd == 1000 /* start motor */) {
        rotate_camera(arg1 != 0, arg2, 0);
        return 0;
    } else if (cmd == 1001 /* stop motor */) {
        android::Mutex::Autolock lock(gRotateLock);
        write_int(MOTOR_ENABLE_FILE, 0);
        return 0;
    }

    return VENDOR_CALL(device, send_command, cmd, arg1, arg2);
}

static void camera_release(struct camera_device *device)
{
    if (!device)
        return;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    VENDOR_CALL(device, release);
}

static int camera_dump(struct camera_device *device, int fd)
{
    if (!device)
        return -EINVAL;

    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    return VENDOR_CALL(device, dump, fd);
}

extern "C" void heaptracker_free_leaked_memory(void);

static int camera_device_close(hw_device_t *device)
{
    int ret = 0;
    wrapper_camera_device_t *wrapper_dev = NULL;

    ALOGV("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraWrapperLock);

    if (!device) {
        ret = -EINVAL;
        goto done;
    }

    wrapper_dev = (wrapper_camera_device_t*) device;
    gUseFlags &= ~(1 << wrapper_dev->id);

    if (gUseFlags == 0) {
        gVendorDeviceHandle->common.close((hw_device_t*) gVendorDeviceHandle);
        rotate_camera(false, 0, 1000);
        free(fixed_set_params);
        gVendorDeviceHandle = NULL;
    }

    if (wrapper_dev->base.ops)
        free(wrapper_dev->base.ops);
    free(wrapper_dev);
done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */

static int camera_device_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    int rv = 0;
    int num_cameras = 0;
    int cameraid;
    wrapper_camera_device_t *camera_device = NULL;
    camera_device_ops_t *camera_ops = NULL;

    android::Mutex::Autolock lock(gCameraWrapperLock);

    ALOGV("%s", __FUNCTION__);

    if (name != NULL) {
        if (check_vendor_module())
            return -EINVAL;

        cameraid = atoi(name);
        num_cameras = gVendorModule->get_number_of_cameras();

        if (cameraid > num_cameras) {
            ALOGE("camera service provided cameraid out of bounds, "
                    "cameraid = %d, num supported = %d",
                    cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }

        if (gUseFlags & (1 << cameraid)) {
            ALOGE("Camera ID %d is already in use");
            rv = -ENODEV;
            goto fail;
        }

        camera_device = (wrapper_camera_device_t*)malloc(sizeof(*camera_device));
        if (!camera_device) {
            ALOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }
        memset(camera_device, 0, sizeof(*camera_device));
        camera_device->id = cameraid;

        if (!gVendorDeviceHandle) {
            rv = gVendorModule->common.methods->open(
                    (const hw_module_t*)gVendorModule, "0",
                    (hw_device_t**)&(gVendorDeviceHandle));
            if (rv) {
                ALOGE("vendor camera open fail");
                goto fail;
            }
        }

        gUseFlags |= (1 << cameraid);

        camera_device->vendor = gVendorDeviceHandle;
        ALOGV("%s: got vendor camera device 0x%08X",
                __FUNCTION__, (uintptr_t)(camera_device->vendor));

        camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
        if (!camera_ops) {
            ALOGE("camera_ops allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        rotate_camera(cameraid == 1, 0, 200);

        memset(camera_ops, 0, sizeof(*camera_ops));

        camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
        camera_device->base.common.module = (hw_module_t *)(module);
        camera_device->base.common.close = camera_device_close;
        camera_device->base.ops = camera_ops;

        camera_ops->set_preview_window = camera_set_preview_window;
        camera_ops->set_callbacks = camera_set_callbacks;
        camera_ops->enable_msg_type = camera_enable_msg_type;
        camera_ops->disable_msg_type = camera_disable_msg_type;
        camera_ops->msg_type_enabled = camera_msg_type_enabled;
        camera_ops->start_preview = camera_start_preview;
        camera_ops->stop_preview = camera_stop_preview;
        camera_ops->preview_enabled = camera_preview_enabled;
        camera_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        camera_ops->start_recording = camera_start_recording;
        camera_ops->stop_recording = camera_stop_recording;
        camera_ops->recording_enabled = camera_recording_enabled;
        camera_ops->release_recording_frame = camera_release_recording_frame;
        camera_ops->auto_focus = camera_auto_focus;
        camera_ops->cancel_auto_focus = camera_cancel_auto_focus;
        camera_ops->take_picture = camera_take_picture;
        camera_ops->cancel_picture = camera_cancel_picture;
        camera_ops->set_parameters = camera_set_parameters;
        camera_ops->get_parameters = camera_get_parameters;
        camera_ops->put_parameters = camera_put_parameters;
        camera_ops->send_command = camera_send_command;
        camera_ops->release = camera_release;
        camera_ops->dump = camera_dump;

        *device = &camera_device->base.common;
    }

    return rv;

fail:
    if (camera_device) {
        free(camera_device);
        camera_device = NULL;
    }
    if (camera_ops) {
        free(camera_ops);
        camera_ops = NULL;
    }
    *device = NULL;
    return rv;
}

static int camera_get_number_of_cameras(void)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;
    return 2; //gVendorModule->get_number_of_cameras();
}

static int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;

    int result = gVendorModule->get_camera_info(0, info);
    if (result == 0 && camera_id == 1) {
        info->facing = CAMERA_FACING_FRONT;
        if (info->orientation >= 180) {
            info->orientation -= 180;
        } else {
            info->orientation += 180;
        }
    }

    return result;
}
