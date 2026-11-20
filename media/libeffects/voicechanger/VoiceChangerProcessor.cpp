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

#include <algorithm>
#include <cmath>

#include <android-base/logging.h>

#include "VoiceChangerProcessor.h"

namespace aidl::android::hardware::audio::effect {

// namespace alias so the enum constants used below resolve.
namespace {
using namespace aidl::android::hardware::audio::effect;
constexpr double kPi = 3.14159265358979323846;
}

const VoiceChangerProcessor::PresetParams& VoiceChangerProcessor::preset(int index) {
    static const PresetParams kPresets[VC_PRESET_COUNT] = {
            // pitch  lowDb  peakDb  peakHz highDb ringHz ringDepth drive gain
            {1.00f, 0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},   // Original
            {0.82f, 4.0f, -2.0f, 900.0f, -2.0f, 0.0f, 0.0f, 1.0f, 1.0f},  // Male
            {1.22f, -2.0f, 2.0f, 1200.0f, 3.5f, 0.0f, 0.0f, 1.0f, 1.0f},  // Female
            {1.42f, -4.0f, 3.0f, 2500.0f, 5.0f, 0.0f, 0.0f, 1.0f, 1.0f},  // Child
            {1.00f, 0.0f, 3.0f, 2000.0f, 0.0f, 55.0f, 0.55f, 1.2f, 1.0f},  // Robot
            {0.68f, 7.0f, -3.0f, 1000.0f, -3.0f, 0.0f, 0.0f, 1.15f, 0.95f},// Deep
            {1.55f, -3.0f, 2.0f, 3000.0f, 4.0f, 0.0f, 0.0f, 1.0f, 1.0f},   // High pitch
            {0.56f, 8.0f, 0.0f, 700.0f, 0.0f, 28.0f, 0.35f, 2.2f, 0.90f},  // Monster
            {1.32f, 0.0f, 4.0f, 3000.0f, 0.0f, 130.0f, 0.70f, 1.0f, 1.0f}, // Alien
            {1.65f, 0.0f, 6.0f, 3200.0f, 4.0f, 0.0f, 0.0f, 1.4f, 0.92f},   // Cartoon
    };
    return kPresets[index];
}

void VoiceChangerProcessor::configure(int sampleRate, int channelCount) {
    if (sampleRate <= 0 || channelCount <= 0) {
        LOG(ERROR) << __func__ << " invalid config " << sampleRate << " " << channelCount;
        return;
    }
    mSampleRate = sampleRate;
    mChannels = channelCount;

    // ~80 ms of audio rounded up to a power of two.
    int capacity = 1;
    while (capacity < sampleRate * 80 / 1000) capacity <<= 1;
    // The grain is 1/4 of the ring so both pitch taps stay in bounds.
    mGrain = std::max(256, capacity >> 2);
    mDryDelay = mGrain >> 1;
    mRingSize = capacity;
    mMask = mRingSize - 1;

    mRing.assign(mChannels, std::vector<float>(mRingSize, 0.0f));
    mEq.assign(mChannels, std::vector<VcBiquad>(3));
    mDc.assign(mChannels, VcDcBlocker());
    reset();
    applyPreset();
}

void VoiceChangerProcessor::setPreset(int presetIndex, int intensityPercent) {
    if (presetIndex < 0 || presetIndex >= VC_PRESET_COUNT) {
        LOG(ERROR) << __func__ << " invalid preset " << presetIndex;
        return;
    }
    mPreset = presetIndex;
    mIntensity = std::clamp<int>(intensityPercent, 0, 100) / 100.0f;
    applyPreset();
}

void VoiceChangerProcessor::applyPreset() {
    PresetParams p = preset(mPreset);
    // The pitch ratio interacts with the grain: keep the shift mild when the
    // wet mix is low so the delayed dry/wet blend stays clean.
    p.pitch = 1.0f + (p.pitch - 1.0f) * (0.5f + 0.5f * mIntensity * mIntensity);
    mParams = p;

    mLfoStep = mParams.ringHz > 0.0f ? (2.0 * kPi * mParams.ringHz) / mSampleRate : 0.0;
    setupFilters();
}

void VoiceChangerProcessor::setupFilters() {
    if (mEq.empty() || mEq[0].size() < 3) return;

    const double fs = mSampleRate;
    const double w0 = 2.0 * kPi * 200.0 / fs;
    const double A = pow(10.0, mParams.lowDb / 40.0);
    const double alpha = sin(w0) / (2.0 * 0.707);
    const double cosw0 = cos(w0);
    const double beta = 2.0 * sqrt(A) * alpha;
    // Low shelf.
    auto& low = mEq[0][0];
    low.setCoeffs(A * ((A + 1.0) - (A - 1.0) * cosw0 + beta),
                  -2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0),
                  A * ((A + 1.0) - (A - 1.0) * cosw0 - beta),
                  ((A + 1.0) + (A - 1.0) * cosw0 + beta),
                  2.0 * ((A - 1.0) + (A + 1.0) * cosw0),
                  ((A + 1.0) + (A - 1.0) * cosw0 - beta));

    // Peaking at mParams.peakHz.
    const double w1 = 2.0 * kPi * mParams.peakHz / fs;
    const double A1 = pow(10.0, mParams.peakDb / 40.0);
    const double alpha1 = sin(w1) / (2.0 * 1.0);
    auto& peak = mEq[0][1];
    peak.setCoeffs(1.0 + alpha1 * A1, -2.0 * cos(w1), 1.0 - alpha1 * A1, 1.0 + alpha1 / A1,
                   -2.0 * cos(w1), 1.0 - alpha1 / A1);

    // High shelf at 4k5.
    const double w2 = 2.0 * kPi * 4500.0 / fs;
    const double A2 = pow(10.0, mParams.highDb / 40.0);
    const double alpha2 = sin(w2) / (2.0 * 0.707);
    const double cosw2 = cos(w2);
    const double beta2 = 2.0 * sqrt(A2) * alpha2;
    auto& high = mEq[0][2];
    high.setCoeffs(A2 * ((A2 + 1.0) + (A2 - 1.0) * cosw2 + beta2),
                   -2.0 * A2 * ((A2 - 1.0) + (A2 + 1.0) * cosw2),
                   A2 * ((A2 + 1.0) + (A2 - 1.0) * cosw2 - beta2),
                   ((A2 + 1.0) - (A2 - 1.0) * cosw2 + beta2),
                   -2.0 * ((A2 - 1.0) - (A2 + 1.0) * cosw2),
                   ((A2 + 1.0) - (A2 - 1.0) * cosw2 - beta2));

    for (int c = 1; c < mChannels; ++c) {
        mEq[c][0] = mEq[0][0];
        mEq[c][1] = mEq[0][1];
        mEq[c][2] = mEq[0][2];
    }
}

void VoiceChangerProcessor::reset() {
    mPhase = 0.0f;
    mLfoPhase = 0.0f;
    mWrite = 0;
    for (auto& ring : mRing) std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& filters : mEq) {
        for (auto& f : filters) {
            f.s1 = 0.0;
            f.s2 = 0.0;
        }
    }
    for (auto& dc : mDc) {
        dc.xPrev = 0.0f;
        dc.yPrev = 0.0f;
    }
}

float VoiceChangerProcessor::delayed(const std::vector<float>& ring, float delay) const {
    if (delay <= 0.0f) return ring[mWrite];
    const int i0 = static_cast<int>(delay);
    const float frac = delay - i0;
    const int idx0 = (mWrite - i0) & mMask;
    const int idx1 = (mWrite - i0 - 1) & mMask;
    return ring[idx0] * (1.0f - frac) + ring[idx1] * frac;
}

void VoiceChangerProcessor::process(float* in, float* out, int frames) {
    if (in == nullptr || out == nullptr || frames <= 0) return;

    // Original preset or zero intensity: copy the input untouched.
    const bool bypass = (mPreset == VC_PRESET_ORIGINAL || mIntensity <= 0.0f);
    if (bypass) {
        std::copy(in, in + frames * mChannels, out);
        return;
    }

    const float wet = mIntensity;
    // Geometric similarity keeps the phase sweep pitch-stable across channels.
    const float inc = 1.0f - mParams.pitch;

    for (int i = 0; i < frames; ++i) {
        const int offset = i * mChannels;

        // Write current input into every channel ring.
        for (int c = 0; c < mChannels; ++c) {
            mRing[c][mWrite] = in[offset + c];
        }

        // Pitch taps (shared phase), window of 2 grains.
        const float phase = mPhase;
        const float t = phase * (1.0f / mGrain);
        // Hann-ish crossfade weights, wA + wB = 1.
        const float wA = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(kPi) * t);
        const float wB = 1.0f - wA;
        float delayA = phase;
        float delayB = phase + mGrain * 0.5f;
        if (delayB >= mGrain) delayB -= mGrain;

        mPhase += inc;
        if (mPhase >= mGrain) mPhase -= mGrain;
        if (mPhase < 0.0f) mPhase += mGrain;

        // Ring modulator LFO shared sample value.
        float lfo = 1.0f;
        if (mLfoStep > 0.0) {
            lfo = static_cast<float>(std::sin(mLfoPhase));
            mLfoPhase += mLfoStep;
            if (mLfoPhase >= 2.0 * kPi) mLfoPhase -= 2.0 * kPi;
            lfo = (1.0f - mParams.ringDepth) + mParams.ringDepth * lfo;
        }

        for (int c = 0; c < mChannels; ++c) {
            const std::vector<float>& ring = mRing[c];
            float wetSample =
                    wA * delayed(ring, delayA) + wB * delayed(ring, delayB);

            // EQ per channel.
            wetSample = mEq[c][0].process(wetSample);
            wetSample = mEq[c][1].process(wetSample);
            wetSample = mEq[c][2].process(wetSample);

            // Ring modulation.
            if (mLfoStep > 0.0) wetSample *= lfo;

            // Soft clip.
            float y = wetSample * mParams.drive;
            if (y > 1.0f) {
                y = 1.0f;
            } else if (y < -1.0f) {
                y = -1.0f;
            } else {
                y = 1.5f * y - 0.5f * y * y * y;
            }
            y *= mParams.gain;
            y = mDc[c].process(y);
            y = std::clamp(y, -1.0f, 1.0f);

            // Dry/wet mix with matching dry delay to avoid comb filtering.
            const float dry = delayed(ring, mDryDelay);
            out[offset + c] = dry + (y - dry) * wet;
        }

        mWrite = (mWrite + 1) & mMask;
    }
}

}  // namespace aidl::android::hardware::audio::effect
