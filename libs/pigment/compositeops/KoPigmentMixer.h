/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Kubelka-Munk pigment mixing, 7-primary model.
 *
 *  The spectral basis (7 base reflectance curves + D65-weighted CIE CMFs) is from
 *  spectral.js 3.0.0, Copyright (c) 2025 Ronald van Wijnen, MIT License:
 *
 *    Permission is hereby granted, free of charge, to any person obtaining a copy
 *    of this software and associated documentation files (the "Software"), to deal
 *    in the Software without restriction, including without limitation the rights
 *    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *    copies of the Software, and to permit persons to whom the Software is
 *    furnished to do so, subject to the following conditions:
 *    The above copyright notice and this permission notice shall be included in all
 *    copies or substantial portions of the Software.
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 *  The original 38 bands are folded into 8 by an optimised contiguous partition
 *  (mean dE_ok 0.12, p99 0.8 against the full 38-band model), so one pigment is
 *  exactly one AVX register / two NEON registers and every per-band loop below
 *  auto-vectorises. Compile the users of this header with -fno-math-errno so
 *  sqrt vectorises.
 *
 *  Pipeline for one colour (linear RGB in, "Pigment" out):
 *    linear RGB -> 7 primary weights (W,C,M,Y,R,G,B) -> reflectance R(band)
 *    -> KS = (1-R)^2 / 2R            (absorption / scattering ratio)
 *  Mixing:
 *    KS_mix = sum(c_i * KS_i) / sum(c_i),  c_i = w_i^2 * luminance_i
 *    R_mix  = 1 + KS - sqrt(KS^2 + 2KS)
 *    rgb    = M * R_mix               (CMF and XYZ->RGB folded into one 3x8 matrix)
 *  plus a per-colour residual (what the round trip loses) blended back in, so
 *  mix(a, b, 0) == a exactly and repeated smudging never drifts.
 */

#ifndef KO_PIGMENT_MIXER_H
#define KO_PIGMENT_MIXER_H

#include <algorithm>
#include <cmath>
#include <cstdint>

// The 8-lane vector helpers below are always inlined, so the "AVX vector ABI"
// note GCC/Clang emit for non-AVX builds is irrelevant here.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpsabi"
#endif

#if defined(__GNUC__)
#define KOPIGMENT_INLINE inline __attribute__((always_inline))
#else
#define KOPIGMENT_INLINE inline
#endif

namespace KoPigment {

static constexpr int Bands = 8;

namespace detail {
#include "KoPigmentTables8.inc"
}

/**
 * One colour expressed as paint. Cheap to copy (12 floats + padding).
 * Build once per distinct colour and reuse — this is the expensive half.
 */
struct alignas(32) Pigment
{
    float ks[Bands];      ///< Kubelka-Munk K/S per band
    float lum;            ///< luminance, used for tinting-strength weighting
    float residual[3];    ///< linear RGB lost on round trip, added back after mixing
};

// 8-lane float vector. GCC/Clang vector extensions: AVX/SSE on x86, 2x NEON on arm64.
typedef float v8f __attribute__((vector_size(32)));

KOPIGMENT_INLINE v8f vsqrt(v8f x)
{
#if defined(__clang__) && __clang_major__ >= 16
    return __builtin_elementwise_sqrt(x);
#else
    v8f r;
    for (int i = 0; i < 8; i++) r[i] = std::sqrt(x[i]);
    return r;
#endif
}
KOPIGMENT_INLINE v8f vload(const float *p) { v8f v; __builtin_memcpy(&v, p, sizeof(v)); return v; }
KOPIGMENT_INLINE void vstore(float *p, v8f v) { __builtin_memcpy(p, &v, sizeof(v)); }
KOPIGMENT_INLINE v8f vsplat(float x) { return v8f{x, x, x, x, x, x, x, x}; }
KOPIGMENT_INLINE float vsum(v8f v) { return ((v[0] + v[4]) + (v[1] + v[5])) + ((v[2] + v[6]) + (v[3] + v[7])); }

/// KM inverse: K/S -> reflectance, all bands at once
KOPIGMENT_INLINE v8f ksToR(v8f k) { return 1.0f + k - vsqrt(k * k + 2.0f * k); }

/// reflectance -> linear RGB
KOPIGMENT_INLINE void reflToLinear(v8f R, float out[3])
{
    using namespace detail;
    out[0] = vsum(R * vload(kToLinearRgb[0]));
    out[1] = vsum(R * vload(kToLinearRgb[1]));
    out[2] = vsum(R * vload(kToLinearRgb[2]));
}

/// linear RGB (any range, clamped to >= 0) -> Pigment
KOPIGMENT_INLINE void makePigment(const float rgb[3], Pigment &p)
{
    using namespace detail;
    const float r0 = std::max(rgb[0], 0.0f);
    const float g0 = std::max(rgb[1], 0.0f);
    const float b0 = std::max(rgb[2], 0.0f);

    const float w = std::min(r0, std::min(g0, b0));
    const float r = r0 - w, g = g0 - w, b = b0 - w;

    // white + one secondary + one primary are the only non-zero weights
    const float wc = std::min(g, b), wm = std::min(r, b), wy = std::min(r, g);
    const float wr = std::max(0.0f, std::min(r - b, r - g));
    const float wg = std::max(0.0f, std::min(g - b, g - r));
    const float wb = std::max(0.0f, std::min(b - g, b - r));

    p.lum = std::max(w * kBaseLuminance[0] + wc * kBaseLuminance[1] + wm * kBaseLuminance[2] + wy * kBaseLuminance[3]
                   + wr * kBaseLuminance[4] + wg * kBaseLuminance[5] + wb * kBaseLuminance[6], 1e-7f);

    v8f R = w * vload(kBase[0]) + wc * vload(kBase[1]) + wm * vload(kBase[2]) + wy * vload(kBase[3])
          + wr * vload(kBase[4]) + wg * vload(kBase[5]) + wb * vload(kBase[6]);
    R = R < 1e-6f ? vsplat(1e-6f) : R;

    const v8f oneMinus = 1.0f - R;
    const v8f ks = oneMinus * oneMinus / (2.0f * R);
    vstore(p.ks, ks);

    float rt[3];
    reflToLinear(ksToR(ks), rt);
    p.residual[0] = r0 - rt[0];
    p.residual[1] = g0 - rt[1];
    p.residual[2] = b0 - rt[2];
}

/// K/S spectrum -> linear RGB (no residual)
KOPIGMENT_INLINE void ksToLinear(const float *ks, float out[3])
{
    reflToLinear(ksToR(vload(ks)), out);
}

/**
 * Mix two pigments. \p t is the share of \p b by *amount of paint*, 0..1.
 * Result is linear RGB, clamped to [0, 1].
 */
KOPIGMENT_INLINE void mix(const Pigment &a, const Pigment &b, float t, float out[3])
{
    const float s = 1.0f - t;
    float ca = s * s * a.lum;
    float cb = t * t * b.lum;
    const float inv = 1.0f / (ca + cb);
    ca *= inv; cb *= inv;

    reflToLinear(ksToR(vload(a.ks) * ca + vload(b.ks) * cb), out);
    for (int c = 0; c < 3; c++) {
        out[c] = std::clamp(out[c] + s * a.residual[c] + t * b.residual[c], 0.0f, 1.0f);
    }
}

typedef int32_t v8i __attribute__((vector_size(32)));

/// e^-x for x >= 0, all 8 lanes. Polynomial 2^f with exponent bit-twiddling,
/// ~1e-6 relative error - plenty for reflectances.
KOPIGMENT_INLINE v8f vexpneg(v8f x)
{
    v8f y = x * -1.44269504088896f;            // log2(e^-x)
    y = y < -126.0f ? vsplat(-126.0f) : y;
    const v8i yi = __builtin_convertvector(y, v8i);   // truncates toward 0
    v8f fi = __builtin_convertvector(yi, v8f);
    const v8i adj = fi > y;                     // floor for negatives
    const v8i ii = yi + adj;                    // adj is -1 where we truncated up
    fi = __builtin_convertvector(ii, v8f);
    const v8f f = y - fi;                       // [0, 1)
    // 2^f, minimax degree 5
    v8f p = 1.8775767e-3f + f * 0.0f;
    p = p * f + 8.9893397e-3f;
    p = p * f + 5.5826318e-2f;
    p = p * f + 2.4015361e-1f;
    p = p * f + 6.9315308e-1f;
    p = p * f + 9.9999994e-1f;
    const v8i bits = (ii + 127) << 23;
    v8f scale;
    __builtin_memcpy(&scale, &bits, sizeof(scale));
    return p * scale;
}

/**
 * Kubelka-Munk reflectance of a paint layer of finite thickness over a ground.
 *
 *   Rinf : reflectance of the paint when infinitely thick (its masstone), per band
 *   Rg   : reflectance of what is underneath (white paper = 1), per band
 *   SX   : scattering x thickness. 0 = no paint (you see the ground), large =
 *          masstone. Thin layers let light reach the ground and come back
 *          through the pigment twice, which is why a thin strong blue looks
 *          brighter AND more saturated than a thick one (its undertone).
 *
 *   R = (1 - Rg (a - b coth(b SX))) / (a - Rg + b coth(b SX))
 *   a = (1/Rinf + Rinf) / 2,  b = (1/Rinf - Rinf) / 2
 */
KOPIGMENT_INLINE v8f kmLayer(v8f Rinf, v8f Rg, float SX)
{
    Rinf = Rinf < 1e-4f ? vsplat(1e-4f) : Rinf;
    Rinf = Rinf > 0.9999f ? vsplat(0.9999f) : Rinf;
    const v8f inv = 1.0f / Rinf;
    const v8f a = 0.5f * (inv + Rinf);
    const v8f b = 0.5f * (inv - Rinf);
    v8f x = b * SX;
    x = x < 1e-4f ? vsplat(1e-4f) : x;
    const v8f e = vexpneg(2.0f * x);
    const v8f bc = b * (1.0f + e) / (1.0f - e);     // b coth(b SX)
    return (1.0f - Rg * (a - bc)) / (a - Rg + bc);
}

/**
 * A layer of \p film paint, \p SX thick, over a ground. \p ground null = white
 * paper. Returns linear RGB (clamped) and, in \p cover, how much the paint
 * hides the ground (0 = invisible, 1 = fully hidden) for blending residuals.
 */
KOPIGMENT_INLINE void glaze(const Pigment &film, const Pigment *ground, float SX, float out[3], float *cover = nullptr,
                            float floorR = 0.02f)
{
    // Colours built from sRGB primaries can have ~0% reflectance in some band
    // (a pure blue reflects no red at all), which no real pigment does; that
    // makes thin layers jump to a harsh, fully saturated undertone. Model the
    // layer with a small floor, and fold the difference back in by coverage so
    // thick paint is still exactly the picked colour.
    const v8f RinfTrue = ksToR(vload(film.ks));
    const v8f Rinf = RinfTrue < floorR ? vsplat(floorR) : RinfTrue;
    const v8f Rg = ground ? ksToR(vload(ground->ks)) : vsplat(1.0f);
    const v8f R = kmLayer(Rinf, Rg, SX);

    // coverage = how far R moved from the ground towards the masstone
    const float c = std::clamp(vsum(Rg - R) / std::max(vsum(Rg - Rinf), 1e-5f), 0.0f, 1.0f);

    float rgbR[3], rgbInfF[3], rgbInfTrue[3], rgbG[3];
    reflToLinear(R, rgbR);
    reflToLinear(Rinf, rgbInfF);
    reflToLinear(RinfTrue, rgbInfTrue);
    reflToLinear(Rg, rgbG);
    for (int k = 0; k < 3; k++) {
        const float filmTrue = rgbInfTrue[k] + film.residual[k];
        const float groundTrue = ground ? rgbG[k] + ground->residual[k] : 1.0f;
        out[k] = std::clamp(rgbR[k] + c * (filmTrue - rgbInfF[k]) + (1.0f - c) * (groundTrue - rgbG[k]),
                            0.0f, 1.0f);
    }
    if (cover) *cover = c;
}

/**
 * Mix two pigments and keep the result as a pigment, so it can be mixed again
 * without converting back through RGB. Also returns the linear RGB of the
 * result (clamped to [0, 1]). Same maths as mix().
 */
KOPIGMENT_INLINE void mixPigments(const Pigment &a, const Pigment &b, float t, Pigment &out, float rgbOut[3])
{
    const float s = 1.0f - t;
    float ca = s * s * a.lum;
    float cb = t * t * b.lum;
    const float inv = 1.0f / (ca + cb);
    ca *= inv; cb *= inv;

    const v8f ks = vload(a.ks) * ca + vload(b.ks) * cb;
    vstore(out.ks, ks);

    float rgb[3];
    reflToLinear(ksToR(ks), rgb);
    for (int c = 0; c < 3; c++) {
        out.residual[c] = s * a.residual[c] + t * b.residual[c];
        rgbOut[c] = std::clamp(rgb[c] + out.residual[c], 0.0f, 1.0f);
    }
    out.lum = std::max(0.2126f * rgbOut[0] + 0.7152f * rgbOut[1] + 0.0722f * rgbOut[2], 1e-7f);
}

/**
 * Small direct-mapped cache of colour -> pigment. Painted areas repeat colours
 * a lot (flat fills, the inside of a stroke), and building a pigment is the
 * expensive half of mixing. The key is any 64-bit packing of the colour.
 */
template<int Size = 64>
struct PigmentHashCache
{
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");
    uint64_t keys[Size];
    Pigment pigments[Size];

    PigmentHashCache() { for (int i = 0; i < Size; i++) keys[i] = ~uint64_t(0); }

    /// \p rgb is only read on a miss
    template<typename RgbFn>
    KOPIGMENT_INLINE const Pigment &get(uint64_t key, RgbFn &&rgbFn)
    {
        const unsigned idx = unsigned((key * 0x9E3779B97F4A7C15ull) >> 40) & (Size - 1);
        if (keys[idx] != key) {
            float rgb[3];
            rgbFn(rgb);
            makePigment(rgb, pigments[idx]);
            keys[idx] = key;
        }
        return pigments[idx];
    }
};

/**
 * Accumulates any number of weighted pigments — e.g. averaging the paint under
 * a brush for pickup — in K/S space. Weights are amounts of paint.
 */
struct Accumulator
{
    alignas(32) float ks[Bands] = {};
    float residual[3] = {};
    float conc = 0.0f;
    float weight = 0.0f;

    inline void add(const Pigment &p, float w)
    {
        if (w <= 0.0f) return;
        const float c = w * w * p.lum;
        vstore(ks, vload(ks) + vload(p.ks) * c);
        for (int k = 0; k < 3; k++) residual[k] += p.residual[k] * w;
        conc += c;
        weight += w;
    }

    inline bool isEmpty() const { return conc <= 0.0f; }

    /// the mixed colour as a new Pigment (so it can be mixed again)
    inline void result(Pigment &p) const
    {
        const float inv = conc > 0.0f ? 1.0f / conc : 0.0f;
        float lum = 0.0f;
        vstore(p.ks, vload(ks) * inv);
        float rgb[3];
        ksToLinear(p.ks, rgb);
        const float winv = weight > 0.0f ? 1.0f / weight : 0.0f;
        for (int k = 0; k < 3; k++) p.residual[k] = residual[k] * winv;
        // luminance of the mix (Rec.709 Y of the result)
        lum = 0.2126f * (rgb[0] + p.residual[0]) + 0.7152f * (rgb[1] + p.residual[1]) + 0.0722f * (rgb[2] + p.residual[2]);
        p.lum = std::max(lum, 1e-7f);
    }
};

/// Pigment -> linear RGB
inline void toLinear(const Pigment &p, float out[3])
{
    ksToLinear(p.ks, out);
    for (int c = 0; c < 3; c++) out[c] = std::clamp(out[c] + p.residual[c], 0.0f, 1.0f);
}

/*********************************************************************/
/*  sRGB transfer helpers (integer colour spaces are stored encoded)  */
/*********************************************************************/

struct SrgbTables
{
    float decode8[256];
    float decode16[65536];
    uint16_t encode[4097];   // linear [0,1] in 4096 steps -> 16-bit sRGB
    uint8_t encode8tab[16385]; // linear [0,1] in 16384 steps -> 8-bit sRGB, nearest

    SrgbTables()
    {
        for (int i = 0; i < 256; i++) decode8[i] = toLin(i / 255.0);
        for (int i = 0; i < 65536; i++) decode16[i] = toLin(i / 65535.0);
        for (int i = 0; i <= 4096; i++) {
            const double x = i / 4096.0;
            const double s = x > 0.0031308 ? 1.055 * std::pow(x, 1.0 / 2.4) - 0.055 : 12.92 * x;
            encode[i] = uint16_t(std::lround(std::clamp(s, 0.0, 1.0) * 65535.0));
        }
        for (int i = 0; i <= 16384; i++) {
            const double x = i / 16384.0;
            const double s = x > 0.0031308 ? 1.055 * std::pow(x, 1.0 / 2.4) - 0.055 : 12.92 * x;
            encode8tab[i] = uint8_t(std::lround(std::clamp(s, 0.0, 1.0) * 255.0));
        }
    }
    static float toLin(double s) { return float(s > 0.04045 ? std::pow((s + 0.055) / 1.055, 2.4) : s / 12.92); }

    static const SrgbTables &instance() { static const SrgbTables t; return t; }

    /// linear -> 16-bit sRGB, linear interpolation between table entries
    inline uint16_t encode16(float x) const
    {
        x = std::clamp(x, 0.0f, 1.0f) * 4096.0f;
        const int i = std::min(int(x), 4095);
        const float f = x - float(i);
        return uint16_t(float(encode[i]) + (float(encode[i + 1]) - float(encode[i])) * f + 0.5f);
    }
    inline uint8_t encode8(float x) const
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return encode8tab[int(x * 16384.0f + 0.5f)];
    }
};

} // namespace KoPigment

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif // KO_PIGMENT_MIXER_H
