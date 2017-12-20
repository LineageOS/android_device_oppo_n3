/*
   Copyright (c) 2014, The CyanogenMod Project

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
   BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
   OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
   IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>

#include <android-base/logging.h>

#include "vendor_init.h"
#include "property_service.h"
#include "util.h"

#include "init_msm8974.h"

using android::init::import_kernel_cmdline;
using android::init::property_set;

static const struct {
    std::string num_value;
    std::string model;
} RF_VERSION_MAPPING[] = {
    { "90", "N5207" },
    { "91", "N5207" },
    { "95", "N5206" },
    { "96", "N5206" },
    { "97", "N5206" }
};

static void import_kernel_nv(const std::string& key,
        const std::string& value, bool for_emulator __attribute__((unused)))
{
    if (key.empty()) return;

    if (key == "oppo.rf_version") {
        size_t i, count = sizeof(RF_VERSION_MAPPING) / sizeof(RF_VERSION_MAPPING[0]);
        for (i = 0; i < count; i++) {
            if (RF_VERSION_MAPPING[i].num_value == value) {
                property_set("ro.product.model", RF_VERSION_MAPPING[i].model.c_str());
                break;
            }
        }
        if (i == count) {
            // this should never happen, but be safe anyway...
            property_set("ro.product.model", "N520x");
        }
        property_set("ro.oppo.rf_version", value.c_str());
    } else if (key == "oppo.pcb_version") {
        property_set("ro.oppo.pcb_version", value.c_str());
    }
}

void init_target_properties()
{
    import_kernel_cmdline(0, import_kernel_nv);
}
