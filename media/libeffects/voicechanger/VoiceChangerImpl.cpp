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

#define ATRACE_TAG ATRACE_TAG_AUDIO
#define LOG_TAG "AHAL_VoiceChanger"

#include <android-base/logging.h>
#include <aidl/android/hardware/audio/effect/DefaultExtension.h>
#include <aidl/android/hardware/audio/effect/VendorExtension.h>
#include <system/audio_effects/effect_uuid.h>
#include <utils/Trace.h>

#include <cstring>
#include <utility>

#include "VoiceChangerImpl.h"

using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::getVoiceChangerImplUuid;
using aidl::android::hardware::audio::effect::getVoiceChangerTypeUuid;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::VoiceChangerImpl;
using aidl::android::hardware::audio::effect::VC_PARAM_PRESET;
using aidl::android::hardware::audio::effect::VC_PARAM_INTENSITY;
using aidl::android::hardware::audio::effect::VC_PRESET_COUNT;
using aidl::android::media::audio::common::AudioUuid;

extern "C" binder_exception_t createEffect(const AudioUuid* in_impl_uuid,
                                           std::shared_ptr<IEffect>* instanceSpp) {
    if (!in_impl_uuid || *in_impl_uuid != getVoiceChangerImplUuid()) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (instanceSpp) {
        *instanceSpp = ndk::SharedRefBase::make<VoiceChangerImpl>();
        return EX_NONE;
    }
    LOG(ERROR) << __func__ << " invalid input parameter!";
    return EX_ILLEGAL_ARGUMENT;
}

extern "C" binder_exception_t queryEffect(const AudioUuid* in_impl_uuid, Descriptor* _aidl_return) {
    if (!in_impl_uuid || *in_impl_uuid != getVoiceChangerImplUuid()) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    *_aidl_return = VoiceChangerImpl::kDescriptor;
    return EX_NONE;
}

namespace aidl::android::hardware::audio::effect {

const std::string VoiceChangerImpl::kEffectName = "Voice Changer";

const Descriptor VoiceChangerImpl::kDescriptor = {
        .common = {.id = {.type = getVoiceChangerTypeUuid(),
                          .uuid = getVoiceChangerImplUuid(),
                          .proxy = std::nullopt},
                   .flags = {.type = Flags::Type::PRE_PROC},
                   .name = VoiceChangerImpl::kEffectName,
                   .implementor = "The Android Open Source Project"}};

ndk::ScopedAStatus VoiceChangerImpl::getDescriptor(Descriptor* _aidl_return) {
    RETURN_IF(!_aidl_return, EX_ILLEGAL_ARGUMENT, "Parameter:nullptr");
    *_aidl_return = kDescriptor;
    return ndk::ScopedAStatus::ok();
}

void VoiceChangerImpl::reconfigureLocked() {
    if (!mProcessor) return;
    const auto& common = mContext->getCommon();
    const auto& input = common.input.base;
    const int channels = static_cast<int>(mContext->getInputFrameSize() / sizeof(float));
    LOG(INFO) << __func__ << ": rate=" << input.sampleRate << " channels=" << channels
              << " preset=" << mPreset << " intensity=" << mIntensity;
    mProcessor->configure(input.sampleRate, channels);
    mProcessor->setPreset(mPreset, mIntensity);
}

ndk::ScopedAStatus VoiceChangerImpl::setParameterSpecific(const Parameter::Specific& specific) {
    RETURN_IF(Parameter::Specific::vendorEffect != specific.getTag(), EX_ILLEGAL_ARGUMENT,
              "EffectNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto& vendorEffect = specific.get<Parameter::Specific::vendorEffect>();
    std::optional<DefaultExtension> defaultExt;
    RETURN_IF(STATUS_OK != vendorEffect.extension.getParcelable(&defaultExt), EX_ILLEGAL_ARGUMENT,
              "getParcelableFailed");
    RETURN_IF(!defaultExt.has_value(), EX_ILLEGAL_ARGUMENT, "parcelableNull");
    const auto& bytes = defaultExt->bytes;
    RETURN_IF(bytes.size() < sizeof(effect_param_t) + 8, EX_ILLEGAL_ARGUMENT, "paramTooShort");

    const auto* ep = reinterpret_cast<const effect_param_t*>(bytes.data());
    RETURN_IF(ep->psize != sizeof(int32_t) || ep->vsize != sizeof(int32_t), EX_ILLEGAL_ARGUMENT,
              "unsupportedParamSize");
    const uint8_t* data = bytes.data() + sizeof(effect_param_t);
    int32_t paramId = 0;
    int32_t value = 0;
    memcpy(&paramId, data, sizeof(paramId));
    memcpy(&value, data + sizeof(int32_t), sizeof(value));

    switch (paramId) {
        case VC_PARAM_PRESET:
            RETURN_IF(value < 0 || value >= VC_PRESET_COUNT, EX_ILLEGAL_ARGUMENT,
                      "invalidPreset");
            mPreset = value;
            break;
        case VC_PARAM_INTENSITY:
            RETURN_IF(value < 0 || value > 100, EX_ILLEGAL_ARGUMENT, "invalidIntensity");
            mIntensity = value;
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "paramNotSupported");
    }
    LOG(INFO) << __func__ << ": param=" << paramId << " value=" << value;
    if (mProcessor) mProcessor->setPreset(mPreset, mIntensity);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VoiceChangerImpl::getParameterSpecific(const Parameter::Id& id,
                                                          Parameter::Specific* specific) {
    RETURN_IF(!specific, EX_NULL_POINTER, "nullPtr");
    auto tag = id.getTag();
    RETURN_IF(Parameter::Id::vendorEffectTag != tag, EX_ILLEGAL_ARGUMENT, "wrongIdTag");
    auto extensionId = id.get<Parameter::Id::vendorEffectTag>();
    std::optional<DefaultExtension> defaultIdExt;
    RETURN_IF(STATUS_OK != extensionId.extension.getParcelable(&defaultIdExt), EX_ILLEGAL_ARGUMENT,
              "getIdParcelableFailed");
    RETURN_IF(!defaultIdExt.has_value(), EX_ILLEGAL_ARGUMENT, "parcelableIdNull");

    int32_t paramId = 0;
    if (defaultIdExt->bytes.size() >= sizeof(effect_param_t) + sizeof(int32_t)) {
        const auto* ep = reinterpret_cast<const effect_param_t*>(defaultIdExt->bytes.data());
        if (ep->psize >= sizeof(int32_t)) {
            memcpy(&paramId, defaultIdExt->bytes.data() + sizeof(effect_param_t),
                   sizeof(paramId));
        }
    }
    int32_t value = 0;
    switch (paramId) {
        case VC_PARAM_PRESET:
            value = mPreset;
            break;
        case VC_PARAM_INTENSITY:
            value = mIntensity;
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "paramNotSupported");
    }

    std::vector<uint8_t> reply(sizeof(effect_param_t) + 8);
    auto* rep = reinterpret_cast<effect_param_t*>(reply.data());
    rep->status = 0;
    rep->psize = sizeof(int32_t);
    rep->vsize = sizeof(int32_t);
    memcpy(reply.data() + sizeof(effect_param_t), &paramId, sizeof(paramId));
    memcpy(reply.data() + sizeof(effect_param_t) + sizeof(int32_t), &value, sizeof(value));

    DefaultExtension defaultExt;
    defaultExt.bytes = std::move(reply);
    VendorExtension extension;
    RETURN_IF(STATUS_OK != extension.extension.setParcelable(defaultExt), EX_ILLEGAL_ARGUMENT,
              "setParcelableFailed");
    specific->set<Parameter::Specific::vendorEffect>(extension);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<EffectContext> VoiceChangerImpl::createContext(const Parameter::Common& common) {
    if (mProcessor) {
        LOG(DEBUG) << __func__ << " context already exist";
        return mContext;
    }
    if (common.input.base.sampleRate <= 0 || common.input.base.sampleRate !=
                                                      common.output.base.sampleRate) {
        LOG(ERROR) << __func__ << " invalid or non-matching sample rate: "
                   << common.input.base.sampleRate << " / " << common.output.base.sampleRate;
        return nullptr;
    }

    mContext = std::make_shared<EffectContext>(1 /* statusFmqDepth */, common);
    mProcessor = std::make_unique<VoiceChangerProcessor>();
    reconfigureLocked();
    return mContext;
}

RetCode VoiceChangerImpl::releaseContext() {
    mContext.reset();
    mProcessor.reset();
    return RetCode::SUCCESS;
}

ndk::ScopedAStatus VoiceChangerImpl::commandImpl(CommandId id) {
    LOG(INFO) << __func__ << ": id=" << static_cast<int>(id);
    if (mProcessor && (id == CommandId::START || id == CommandId::RESET)) {
        mProcessor->reset();
    }
    return EffectImpl::commandImpl(id);
}

ndk::ScopedAStatus VoiceChangerImpl::setParameterCommon(const Parameter& param) {
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    auto status = EffectImpl::setParameterCommon(param);
    if (status.isOk() && param.getTag() == Parameter::common) {
        reconfigureLocked();
    }
    return status;
}

IEffect::Status VoiceChangerImpl::effectProcessImpl(float* in, float* out, int samples) {
    if (!mContext || !mProcessor) {
        return {EX_NULL_POINTER, 0, 0};
    }
    const int frameSize = static_cast<int>(mContext->getInputFrameSize() / sizeof(float));
    if (frameSize <= 0 || samples % frameSize != 0) {
        return {EX_ILLEGAL_ARGUMENT, 0, 0};
    }
    if (mProcessCallCount < 3 || (mProcessCallCount % 500) == 0) {
        LOG(INFO) << __func__ << ": call=" << mProcessCallCount << " samples=" << samples
                  << " frameSize=" << frameSize << " preset=" << mPreset
                  << " intensity=" << mIntensity;
    }
    ++mProcessCallCount;
    mProcessor->process(in, out, samples / frameSize);
    return {STATUS_OK, samples, samples};
}

}  // namespace aidl::android::hardware::audio::effect
