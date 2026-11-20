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

#include <memory>
#include <string>

#include <aidl/android/hardware/audio/effect/BnEffect.h>
#include <system/audio_effect.h>

#include "VoiceChangerProcessor.h"
#include "VoiceChangerTypes.h"
#include "effect-impl/EffectImpl.h"

namespace aidl::android::hardware::audio::effect {

/**
 * Input (microphone) voice changer effect. Attach to an input device from
 * @code AudioEffect(UUID, AudioDeviceAttributes) @endcode and it pre-processes
 * every capture on that device while enabled.
 */
class VoiceChangerImpl final : public EffectImpl {
  public:
    static const std::string kEffectName;
    static const Descriptor kDescriptor;

    VoiceChangerImpl() = default;
    ~VoiceChangerImpl() { cleanUp(); }

    ndk::ScopedAStatus getDescriptor(Descriptor* _aidl_return) override;
    ndk::ScopedAStatus setParameterSpecific(const Parameter::Specific& specific)
            REQUIRES(mImplMutex) override;
    ndk::ScopedAStatus getParameterSpecific(const Parameter::Id& id, Parameter::Specific* specific)
            REQUIRES(mImplMutex) override;

    std::shared_ptr<EffectContext> createContext(const Parameter::Common& common)
            REQUIRES(mImplMutex) override;
    RetCode releaseContext() REQUIRES(mImplMutex) override;
    ndk::ScopedAStatus commandImpl(CommandId id) REQUIRES(mImplMutex) override;
    ndk::ScopedAStatus setParameterCommon(const Parameter& param)
            REQUIRES(mImplMutex) override;

    IEffect::Status effectProcessImpl(float* in, float* out, int samples)
            REQUIRES(mImplMutex) override;

    std::string getEffectName() override { return kEffectName; }

  private:
    std::shared_ptr<EffectContext> mContext GUARDED_BY(mImplMutex);
    std::unique_ptr<VoiceChangerProcessor> mProcessor GUARDED_BY(mImplMutex);
    int32_t mPreset GUARDED_BY(mImplMutex) = VC_PRESET_ORIGINAL;
    int32_t mIntensity GUARDED_BY(mImplMutex) = 100;
    uint64_t mProcessCallCount GUARDED_BY(mImplMutex) = 0;

    void reconfigureLocked() REQUIRES(mImplMutex);
};

}  // namespace aidl::android::hardware::audio::effect
