/*
 * Copyright (C) 2015 The CyanogenMod Project
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

package com.android.camera.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.Camera;

import com.android.camera.app.DevicePluginBase;
import com.android.ex.camera2.portability.CameraAgent;

public class DevicePluginImpl extends DevicePluginBase {
    private Context mContext;
    private CameraAgent.CameraProxy mCamera;

    private static final int CMD_START_MOTOR = 1000;
    private static final int CMD_STOP_MOTOR = 1001;

    // from OclickService.java
    private static final int KEYCODE_UP = 0x20;
    private static final int KEYCODE_DOWN = 0x40;
    private static final int KEYTYPE_LONG_RELEASE = 0;
    private static final int KEYTYPE_LONG_PRESS = 3;

    private BroadcastReceiver mOclickKeyReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int key = intent.getIntExtra("key", -1);
            int action = intent.getIntExtra("action", -1);
            handleOclickKey(key, action);
        }
    };

    public DevicePluginImpl() {
    }

    @Override
    public void onCreate(Context context) {
        mContext = context;
        mContext.registerReceiver(mOclickKeyReceiver,
                new IntentFilter("com.cyanogenmod.device.oppo.ACTION_OCLICK_KEY"));
    }

    @Override
    public void onDestroy() {
        mContext.unregisterReceiver(mOclickKeyReceiver);
    }

    @Override
    public void onCameraOpened(CameraAgent.CameraProxy camera) {
        mCamera = camera;
    }

    private void handleOclickKey(int key, int action) {
        if (mCamera == null) {
            return;
        }

        final int cmd, direction, speed;
        if (key != KEYCODE_UP && key != KEYCODE_DOWN) {
            return;
        }

        if (action == KEYTYPE_LONG_RELEASE) {
            cmd = CMD_STOP_MOTOR;
            direction = speed = 0;
        } else if (action == KEYTYPE_LONG_PRESS) {
            cmd = CMD_START_MOTOR;
            direction = key == KEYCODE_UP ? 1 : 0;
            speed = 2;
        } else {
            return;
        }

        final Camera camera = mCamera.getCamera();

        mCamera.getCameraHandler().post(new Runnable() {
            @Override
            public void run() {
                camera.sendVendorCommand(cmd, direction, speed);
            }
        });
    }
}
