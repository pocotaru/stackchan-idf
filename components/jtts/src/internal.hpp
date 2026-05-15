// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "jtts/jtts.hpp"
#include "jtts/phoneme.hpp"

namespace stackchan::jtts::internal {

struct FormantFrame {
    float f1 = 500.0f, f2 = 1500.0f, f3 = 2500.0f;
    float bw1 = 70.0f, bw2 = 100.0f, bw3 = 150.0f;
    float a1 = 1.0f, a2 = 0.6f, a3 = 0.3f;
    float voicing = 1.0f;
    float frication = 0.0f;
    float f0_hz = 130.0f;
    float nasal = 0.0f;
};

struct Segment {
    FormantFrame start;
    FormantFrame end;
    float duration_ms = 0.0f;
};

bool parse_kana(std::u32string_view kana, std::vector<Mora>& out);

void build_segments(std::span<const Mora> moras, std::span<Segment> /*unused-placeholder*/);
void build_segments(std::span<const Mora> moras, std::vector<Segment>& out, const Options& opt);

FormantFrame vowel_frame(Vowel v, bool palatalized);
FormantFrame nasal_frame(Consonant c);
FormantFrame consonant_burst(Consonant c, Vowel next_v);

void render_segments(std::span<const Segment> segs, std::vector<std::int16_t>& out, const Options& opt);

}  // namespace stackchan::jtts::internal
