// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "speech.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <M5Unified.h>
#include <esp_timer.h>

#include <jtts/jtts.hpp>

namespace stackchan::app {

namespace {

// 番評語 (係助詞「は/へ」は発音通り「わ/え」と表記、「を」は「お」と書く)。
// 漢字変換は無いので、すべてひらがな + 拗音 + 「ー」「っ」「ん」のみ。
constexpr std::u32string_view kPhrases[] = {
    U"こんにちわー",
    U"おはよー",
    U"やっほー",
    U"あそぼー",
    U"なでなで してー",
    U"おなかすいたー",
    U"げんき げんき",
    U"わたしわ すたっくちゃん",
};

void build_envelope_from_pcm(const std::vector<std::int16_t>& pcm,
                             std::vector<float>& envelope, std::uint32_t sample_rate,
                             std::uint32_t step_ms)
{
    const std::size_t window =
        static_cast<std::size_t>(sample_rate) * static_cast<std::size_t>(step_ms) / 1000u;
    if (window == 0 || pcm.empty()) {
        envelope.clear();
        return;
    }
    const std::size_t windows = (pcm.size() + window - 1) / window;
    envelope.assign(windows, 0.0f);
    for (std::size_t w = 0; w < windows; ++w) {
        const std::size_t begin = w * window;
        const std::size_t end = std::min(begin + window, pcm.size());
        std::int32_t peak = 0;
        for (std::size_t i = begin; i < end; ++i) {
            peak = std::max(peak, std::abs(static_cast<std::int32_t>(pcm[i])));
        }
        envelope[w] = static_cast<float>(peak) / 32767.0f;
    }
}

} // namespace

void Speech::babble(std::uint32_t seed)
{
    constexpr std::size_t kNumPhrases = sizeof(kPhrases) / sizeof(kPhrases[0]);
    const std::u32string_view kana = kPhrases[seed % kNumPhrases];

    stackchan::jtts::Options opt;
    opt.voice = stackchan::jtts::Voice::Female;
    opt.f0_hz = 280.0f;          // child preset (CLI と同一)
    opt.formant_scale = 1.30f;
    opt.mora_ms = 120.0f;
    opt.sample_rate_hz = kSampleRate;

    pcm_.clear();
    auto r = stackchan::jtts::synthesize(kana, pcm_, opt);
    if (!r || pcm_.empty()) {
        return;
    }

    build_envelope_from_pcm(pcm_, envelope_, kSampleRate, kEnvelopeStepMs);

    duration_ms_.store(
        static_cast<std::uint32_t>(static_cast<float>(pcm_.size()) * 1000.0f /
                                   static_cast<float>(kSampleRate)),
        std::memory_order_relaxed);
    start_ms_.store(static_cast<std::uint32_t>(esp_timer_get_time() / 1000),
                    std::memory_order_release);

    M5.Speaker.playRaw(pcm_.data(), pcm_.size(), kSampleRate, /*stereo=*/false,
                       /*repeat=*/1, /*channel=*/-1,
                       /*stop_current_sound=*/true);
}

void Speech::stop()
{
    if (M5.Speaker.isPlaying()) {
        M5.Speaker.stop();
    }
    start_ms_.store(0, std::memory_order_release);
    duration_ms_.store(0, std::memory_order_release);
}

bool Speech::is_speaking() const
{
    const std::uint32_t start = start_ms_.load(std::memory_order_acquire);
    if (start == 0) {
        return false;
    }
    const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    return (now - start) < duration_ms_.load(std::memory_order_relaxed);
}

float Speech::current_mouth_open() const
{
    const std::uint32_t start = start_ms_.load(std::memory_order_acquire);
    if (start == 0 || envelope_.empty()) {
        return 0.0f;
    }
    const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    const std::uint32_t elapsed = now - start;
    if (elapsed >= duration_ms_.load(std::memory_order_relaxed)) {
        return 0.0f;
    }
    const std::size_t idx = elapsed / kEnvelopeStepMs;
    if (idx >= envelope_.size()) {
        return 0.0f;
    }
    return envelope_[idx];
}

} // namespace stackchan::app
