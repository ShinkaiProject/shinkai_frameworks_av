/*
 * Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include <aidl/android/media/audio/common/AudioUuid.h>
#include <system/audio_effects/effect_uuid.h>

namespace aidl::android::hardware::audio::effect {

inline constexpr char kVoiceChangerTypeUuid[] = "b32f39b4-ba49-4e6a-9e3c-2f5c7d1e8a01";
inline constexpr char kVoiceChangerImplUuid[] = "c53f4ac5-8f5a-4d77-a8e4-3a6b9e2f5c02";

inline const AudioUuid& getVoiceChangerTypeUuid() {
    static const AudioUuid uuid = stringToUuid(kVoiceChangerTypeUuid);
    return uuid;
}

inline const AudioUuid& getVoiceChangerImplUuid() {
    static const AudioUuid uuid = stringToUuid(kVoiceChangerImplUuid);
    return uuid;
}

/**
 * Parameters transported through the generic VendorExtension channel as raw
 * effect_param_t bytes (see system/include/media/audio_effect.h).
 */
enum VoiceChangerParam {
    VC_PARAM_PRESET = 0x56430001,     // Value: 0 .. VC_PRESET_COUNT - 1
    VC_PARAM_INTENSITY = 0x56430002,  // Value: 0 .. 100 (percent)
};

enum {
    VC_PRESET_COUNT = 10,
    VC_PRESET_ORIGINAL = 0,
    VC_PRESET_MALE = 1,
    VC_PRESET_FEMALE = 2,
    VC_PRESET_CHILD = 3,
    VC_PRESET_ROBOT = 4,
    VC_PRESET_DEEP = 5,
    VC_PRESET_HIGH = 6,
    VC_PRESET_MONSTER = 7,
    VC_PRESET_ALIEN = 8,
    VC_PRESET_CARTOON = 9,
};

}  // namespace aidl::android::hardware::audio::effect
