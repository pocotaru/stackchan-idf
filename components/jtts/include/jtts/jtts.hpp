// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>

namespace stackchan::jtts {

enum class Voice : std::uint8_t {
    Male,    // 大人男性 (F0 ≈ 130 Hz、フォルマント等倍)
    Female,  // 大人女性 (F0 ≈ 210 Hz、フォルマントを ~17% 持ち上げ)
};

struct Options {
    std::uint32_t sample_rate_hz = 16000;
    Voice voice = Voice::Male;
    // 0 を指定すると voice のデフォルトを使う。明示すると上書き。
    float f0_hz = 0.0f;
    float formant_scale = 0.0f;
    float mora_ms = 110.0f;
    float gain = 0.6f;

    // ----- 声色 (timbre) -----
    // 声帯駆動成分にノイズをどれだけ混ぜるか。0=純声、1=完全に息のみ。
    float breathiness = 0.0f;
    // 有声成分の全体スケール。0 にすると囁き声 (breathiness=1 と併用)。
    float voicing_mul = 1.0f;
    // 無声 (摩擦) 成分の全体スケール。
    float frication_mul = 1.0f;
    // F0 ビブラート (0 = OFF)。rate は LFO 周波数 Hz、depth はセント単位
    // (100 セント = 1 半音)。老人の震え声には rate≈4-5, depth≈30-50 が良い。
    float vibrato_rate_hz = 0.0f;
    float vibrato_cents = 0.0f;
};

enum class Error {
    InvalidKana,
    OutOfMemory,
};

const char* to_string(Error e);

tl::expected<void, Error> synthesize(std::u32string_view kana,
                                     std::vector<std::int16_t>& out,
                                     const Options& opt = {});

}  // namespace stackchan::jtts
