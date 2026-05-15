// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include "internal.hpp"

namespace stackchan::jtts::internal {

namespace {

constexpr float kPi = 3.14159265358979323846f;

class Biquad {
public:
    void set_bpf(float f0, float bw, float fs) {
        if (f0 < 50.0f) f0 = 50.0f;
        if (f0 > fs * 0.45f) f0 = fs * 0.45f;
        if (bw < 30.0f) bw = 30.0f;
        if (bw > f0 * 2.0f) bw = f0 * 2.0f;
        float w0 = 2.0f * kPi * f0 / fs;
        float Q = f0 / bw;
        float alpha = std::sin(w0) / (2.0f * Q);
        float cos_w0 = std::cos(w0);
        float a0 = 1.0f + alpha;
        b0_ = alpha / a0;
        b1_ = 0.0f;
        b2_ = -alpha / a0;
        a1_ = -2.0f * cos_w0 / a0;
        a2_ = (1.0f - alpha) / a0;
    }
    float process(float x) {
        float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }
    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

private:
    float b0_ = 0, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// f0 の周期ごとに高さ ≈ sqrt(Fs/f0) のインパルスを出す励起源。
// BPF resonator (peak gain ≈ 0 dB) を通したときの出力 RMS が f0 に対して
// だいたい一定になるよう正規化している。
class ImpulseSource {
public:
    float tick(float f0_hz, float fs) {
        if (f0_hz <= 1.0f) return 0.0f;
        float inc = f0_hz / fs;
        phase_ += inc;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            return std::sqrt(fs / f0_hz);
        }
        return 0.0f;
    }
    void reset() { phase_ = 0.0f; }

private:
    float phase_ = 0.0f;
};

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline std::int16_t to_i16(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return static_cast<std::int16_t>(x * 32760.0f);
}

}  // namespace

void render_segments(std::span<const Segment> segs, std::vector<std::int16_t>& out,
                     const Options& opt) {
    const float fs = static_cast<float>(opt.sample_rate_hz);
    Biquad r1, r2, r3;
    ImpulseSource voice;
    std::mt19937 rng(0xC0DECAFEu);
    auto noise = [&]() {
        std::uint32_t v = rng();
        return (static_cast<float>(v) / static_cast<float>(0xFFFFFFFFu)) * 2.0f - 1.0f;
    };

    constexpr float step_ms = 5.0f;
    const std::size_t step_samples =
        std::max<std::size_t>(1, static_cast<std::size_t>(step_ms * 0.001f * fs));

    const bool vibrato_on = opt.vibrato_rate_hz > 0.0f && opt.vibrato_cents > 0.0f;
    const float vib_inc = opt.vibrato_rate_hz / fs;
    float vib_phase = 0.0f;
    // ノイズは impulse train (RMS≈1) と振幅を揃えるため √3 倍する (一様乱数 [-1,1]
    // の RMS = 1/√3)。これで breathiness の知覚的ミックスが線形になる。
    constexpr float kNoiseGain = 1.73205081f;

    for (const Segment& seg : segs) {
        std::size_t total = static_cast<std::size_t>(std::lround(seg.duration_ms * 0.001f * fs));
        if (total == 0) continue;

        std::size_t k = 0;
        while (k < total) {
            std::size_t n = std::min(step_samples, total - k);
            float t = static_cast<float>(k + n / 2) / static_cast<float>(total);

            float f1 = lerp(seg.start.f1, seg.end.f1, t);
            float f2 = lerp(seg.start.f2, seg.end.f2, t);
            float f3 = lerp(seg.start.f3, seg.end.f3, t);
            float bw1 = lerp(seg.start.bw1, seg.end.bw1, t);
            float bw2 = lerp(seg.start.bw2, seg.end.bw2, t);
            float bw3 = lerp(seg.start.bw3, seg.end.bw3, t);
            float a1 = lerp(seg.start.a1, seg.end.a1, t);
            float a2 = lerp(seg.start.a2, seg.end.a2, t);
            float a3 = lerp(seg.start.a3, seg.end.a3, t);
            float voicing = lerp(seg.start.voicing, seg.end.voicing, t);
            float frication = lerp(seg.start.frication, seg.end.frication, t);
            float f0_base = lerp(seg.start.f0_hz, seg.end.f0_hz, t);

            r1.set_bpf(f1, bw1, fs);
            r2.set_bpf(f2, bw2, fs);
            r3.set_bpf(f3, bw3, fs);

            for (std::size_t j = 0; j < n; ++j) {
                float f0_mod = f0_base;
                if (vibrato_on) {
                    vib_phase += vib_inc;
                    if (vib_phase >= 1.0f) vib_phase -= 1.0f;
                    float lfo = std::sin(2.0f * kPi * vib_phase);
                    f0_mod = f0_base * std::exp2(opt.vibrato_cents / 1200.0f * lfo);
                }

                float pulse = voice.tick(f0_mod, fs);
                float n1 = noise() * kNoiseGain;
                float n2 = noise() * kNoiseGain;
                float voiced_src = (1.0f - opt.breathiness) * pulse + opt.breathiness * n1;
                float ex = voicing * voiced_src * opt.voicing_mul +
                           frication * n2 * opt.frication_mul;
                float y = a1 * r1.process(ex) + a2 * r2.process(ex) + a3 * r3.process(ex);
                y *= opt.gain;
                out.push_back(to_i16(y));
            }
            k += n;
        }
    }
}

}  // namespace stackchan::jtts::internal
