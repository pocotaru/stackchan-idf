// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0

#include "speech.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#include <M5Unified.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace stackchan::app {

namespace {

constexpr const char* kTag = "speech";

// Compile-time defaults — used when no JttsConfig has been written over BLE
// (fresh device, or empty JSON). The phrase set is hiragana + 拗音 + 「ー」
// 「っ」「ん」 only — jtts has no kana-to-phoneme dictionary so kanji are
// rejected as InvalidKana.
constexpr std::u32string_view kDefaultPhrases[] = {
    U"こんにちわー",
    U"おはよー",
    U"やっほー",
    U"あそぼー",
    U"なでなで してー",
    U"おなかすいたー",
    U"げんき げんき",
    U"わたしわ すたっくちゃん",
};

jtts::Options default_options(std::uint32_t sample_rate)
{
    jtts::Options opt;
    opt.voice = jtts::Voice::Female;
    opt.f0_hz = 280.0f;       // child preset
    opt.formant_scale = 1.30f;
    opt.mora_ms = 120.0f;
    opt.sample_rate_hz = sample_rate;
    return opt;
}

// cJSON helpers. Numeric fields are accepted as JSON numbers; voice is a
// string ("male"/"female"); missing fields keep their current value.
void apply_voice(jtts::Options& opt, const cJSON* item)
{
    if (!cJSON_IsString(item) || item->valuestring == nullptr) return;
    if (std::strcmp(item->valuestring, "male") == 0) opt.voice = jtts::Voice::Male;
    else if (std::strcmp(item->valuestring, "female") == 0) opt.voice = jtts::Voice::Female;
}

void apply_number(float& dst, const cJSON* item)
{
    if (cJSON_IsNumber(item)) dst = static_cast<float>(item->valuedouble);
}

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

void Speech::configure(const std::string& json)
{
    // Compile-time fallback always runs first so configure() is idempotent
    // and missing JSON fields don't pick up stale state.
    opts_ = default_options(kSampleRate);
    phrases_.clear();
    for (auto v : kDefaultPhrases) {
        phrases_.emplace_back(v);
    }
    initialised_ = true;

    if (json.empty()) {
        return;
    }
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        ESP_LOGW(kTag, "jtts config: JSON parse failed, using defaults");
        return;
    }

    apply_voice(opts_, cJSON_GetObjectItemCaseSensitive(root, "voice"));
    apply_number(opts_.f0_hz, cJSON_GetObjectItemCaseSensitive(root, "f0_hz"));
    apply_number(opts_.formant_scale, cJSON_GetObjectItemCaseSensitive(root, "formant_scale"));
    apply_number(opts_.mora_ms, cJSON_GetObjectItemCaseSensitive(root, "mora_ms"));
    apply_number(opts_.gain, cJSON_GetObjectItemCaseSensitive(root, "gain"));
    apply_number(opts_.breathiness, cJSON_GetObjectItemCaseSensitive(root, "breathiness"));
    apply_number(opts_.voicing_mul, cJSON_GetObjectItemCaseSensitive(root, "voicing_mul"));
    apply_number(opts_.frication_mul, cJSON_GetObjectItemCaseSensitive(root, "frication_mul"));
    apply_number(opts_.vibrato_rate_hz, cJSON_GetObjectItemCaseSensitive(root, "vibrato_rate_hz"));
    apply_number(opts_.vibrato_cents, cJSON_GetObjectItemCaseSensitive(root, "vibrato_cents"));

    const cJSON* phrases = cJSON_GetObjectItemCaseSensitive(root, "phrases");
    if (cJSON_IsArray(phrases)) {
        std::vector<std::u32string> parsed;
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, phrases) {
            if (!cJSON_IsString(item) || item->valuestring == nullptr) continue;
            auto u32 = decode_utf8(item->valuestring);
            if (!u32.empty()) parsed.push_back(std::move(u32));
        }
        if (!parsed.empty()) phrases_ = std::move(parsed);
    }
    cJSON_Delete(root);
    ESP_LOGI(kTag, "jtts config: voice=%s f0=%.0f mora=%.0fms phrases=%zu",
             opts_.voice == jtts::Voice::Female ? "female" : "male",
             opts_.f0_hz, opts_.mora_ms, phrases_.size());
}

void Speech::babble(std::uint32_t seed)
{
    if (!initialised_) {
        configure(""); // first-call lazy init with defaults
    }
    if (phrases_.empty()) {
        return;
    }
    const std::u32string& kana = phrases_[seed % phrases_.size()];

    jtts::Options opt = opts_;
    opt.sample_rate_hz = kSampleRate; // playback rate is fixed for envelope sync

    pcm_.clear();
    auto r = jtts::synthesize(kana, pcm_, opt);
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
