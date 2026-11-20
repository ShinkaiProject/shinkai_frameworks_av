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

#include <cstdint>
#include <vector>

#include "VoiceChangerTypes.h"

namespace aidl::android::hardware::audio::effect {

/** Per-channel state of one second order section (RBJ biquad, transposed DF-II). */
struct VcBiquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;

    void setCoeffs(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0;
        b1 = nb1 / na0;
        b2 = nb2 / na0;
        a1 = na1 / na0;
        a2 = na2 / na0;
        s1 = 0.0;
        s2 = 0.0;
    }

    float process(float x) {
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

/** DC blocker, one-pole high pass, state per channel. */
struct VcDcBlocker {
    float yPrev = 0.0f;
    float xPrev = 0.0f;
    float process(float x) {
        const float y = x - xPrev + 0.995f * yPrev;
        xPrev = x;
        yPrev = y;
        return y;
    }
};

/**
 * Real time voice processing: granular dual-tap pitch shifter, 3 band EQ,
 * ring modulation, soft clip and dry/wet mix. All state lives on the audio
 * thread and is guarded by the EffectImpl mutex held by the caller.
 */
class VoiceChangerProcessor {
  public:
    VoiceChangerProcessor() = default;
    ~VoiceChangerProcessor() = default;

    /** (Re)configure for a new sample rate / channel count, keeps preset+intensity. */
    void configure(int sampleRate, int channelCount);
    /** Recompute DSP parameters for the given preset and wet level. */
    void setPreset(int preset, int intensityPercent);
    /** Clear all delay and filter state; safe to call from START/RESET. */
    void reset();
    /** Process interleaved [frames] * channelCount floats. */
    void process(float* in, float* out, int frames);

  private:
    struct PresetParams {
        float pitch;      // read speed ratio, 1.0 = unchanged
        float lowDb;      // low shelf gain in dB
        float peakDb;     // peaking gain in dB
        float peakHz;     // peaking frequency
        float highDb;     // high shelf gain in dB
        float ringHz;     // ring modulator LFO frequency, 0 = off
        float ringDepth;  // 0..1
        float drive;      // pre gain before soft clip, 1.0 = off
        float gain;       // output trim
    };

    static const PresetParams& preset(int index);

    void applyPreset();
    void setupFilters();
    float delayed(const std::vector<float>& ring, float delay) const;

    int mSampleRate = 48000;
    int mChannels = 1;
    int mPreset = VC_PRESET_ORIGINAL;
    float mIntensity = 1.0f;  // 0..1 wet level

    int mRingSize = 4096;
    int mMask = 4095;
    int mGrain = 1024;
    int mDryDelay = 0;
    float mPhase = 0.0f;
    int mWrite = 0;
    float mLfoPhase = 0.0f;
    double mLfoStep = 0.0;

    PresetParams mParams = {};
    std::vector<std::vector<float>> mRing;
    std::vector<std::vector<VcBiquad>> mEq;  // [channel][3]
    std::vector<VcDcBlocker> mDc;
};

}  // namespace aidl::android::hardware::audio::effect
