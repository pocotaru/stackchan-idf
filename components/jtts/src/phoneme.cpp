// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
#include <algorithm>

#include "internal.hpp"

namespace stackchan::jtts::internal {

namespace {

FormantFrame silent_frame(float f0) {
    FormantFrame s;
    s.voicing = 0.0f;
    s.frication = 0.0f;
    s.a1 = s.a2 = s.a3 = 0.0f;
    s.f0_hz = f0;
    return s;
}

void add_cv_segments(Consonant c, Vowel v, bool palatalized, float mora_ms, float f0,
                     std::vector<Segment>& out) {
    FormantFrame vowel = vowel_frame(v, palatalized);
    vowel.f0_hz = f0;

    if (c == Consonant::None) {
        out.push_back({vowel, vowel, mora_ms});
        return;
    }

    FormantFrame burst = consonant_burst(c, v);
    burst.f0_hz = f0;
    if (palatalized && !is_nasal(c)) {
        burst.f2 += 200.0f;
    }

    const FormantFrame silence = silent_frame(f0);

    auto push_vowel_tail = [&](float consumed) {
        float v_ms = std::max(20.0f, mora_ms - consumed);
        out.push_back({vowel, vowel, v_ms});
    };

    if (is_voiceless_stop(c)) {
        out.push_back({silence, silence, 30.0f});
        out.push_back({burst, burst, 10.0f});
        out.push_back({burst, vowel, 30.0f});
        push_vowel_tail(70.0f);
    } else if (is_voiced_stop(c)) {
        out.push_back({burst, burst, 15.0f});
        out.push_back({burst, vowel, 30.0f});
        push_vowel_tail(45.0f);
    } else if (is_voiceless_fric(c)) {
        out.push_back({burst, burst, 70.0f});
        out.push_back({burst, vowel, 20.0f});
        push_vowel_tail(90.0f);
    } else if (is_voiced_fric_affric(c)) {
        out.push_back({burst, burst, 50.0f});
        out.push_back({burst, vowel, 25.0f});
        push_vowel_tail(75.0f);
    } else if (is_affricate(c)) {
        out.push_back({silence, silence, 25.0f});
        out.push_back({burst, burst, 40.0f});
        out.push_back({burst, vowel, 20.0f});
        push_vowel_tail(85.0f);
    } else if (is_nasal(c)) {
        FormantFrame nf = nasal_frame(c);
        nf.f0_hz = f0;
        out.push_back({nf, nf, 50.0f});
        out.push_back({nf, vowel, 20.0f});
        push_vowel_tail(70.0f);
    } else if (is_glide(c)) {
        out.push_back({burst, vowel, 50.0f});
        push_vowel_tail(50.0f);
    } else if (c == Consonant::R) {
        out.push_back({burst, vowel, 25.0f});
        push_vowel_tail(25.0f);
    } else {
        out.push_back({vowel, vowel, mora_ms});
    }
}

}  // namespace

void build_segments(std::span<const Mora> moras, std::vector<Segment>& out, const Options& opt) {
    out.clear();
    const float f0 = opt.f0_hz;
    const float mora_ms = opt.mora_ms;

    for (std::size_t i = 0; i < moras.size(); ++i) {
        const Mora& m = moras[i];
        switch (m.kind) {
            case MoraKind::CV:
                add_cv_segments(m.c, m.v, m.palatalized, mora_ms, f0, out);
                break;
            case MoraKind::MoraicN: {
                Consonant nasal_c = Consonant::N;
                if (i + 1 < moras.size() && moras[i + 1].kind == MoraKind::CV) {
                    Consonant nc = moras[i + 1].c;
                    if (nc == Consonant::M || nc == Consonant::B || nc == Consonant::P) {
                        nasal_c = Consonant::M;
                    }
                }
                FormantFrame nf = nasal_frame(nasal_c);
                nf.f0_hz = f0;
                out.push_back({nf, nf, mora_ms});
                break;
            }
            case MoraKind::Sokuon: {
                FormantFrame s = silent_frame(f0);
                out.push_back({s, s, 70.0f});
                break;
            }
            case MoraKind::Chouon: {
                if (!out.empty()) {
                    FormantFrame ref = out.back().end;
                    out.push_back({ref, ref, mora_ms});
                }
                break;
            }
        }
    }
}

void build_segments(std::span<const Mora>, std::span<Segment>) {}

}  // namespace stackchan::jtts::internal
