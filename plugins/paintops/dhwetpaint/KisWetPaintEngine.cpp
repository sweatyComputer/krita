/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace KoPigment;

namespace {

constexpr float inv255 = 1.0f / 255.0f;
constexpr float inv65535 = 1.0f / 65535.0f;

inline float smoothstep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// a rate defined per step, applied over \p steps steps
inline float overSteps(float ratePerStep, float steps)
{
    ratePerStep = std::clamp(ratePerStep, 0.0f, 0.999f);
    return 1.0f - std::pow(1.0f - ratePerStep, steps);
}

/// mix two pigments by amount and return the result as a pigment
inline void mixToPigment(const Pigment &a, float amountA, const Pigment &b, float amountB, Pigment &out)
{
    Accumulator acc;
    acc.add(a, amountA);
    acc.add(b, amountB);
    if (acc.isEmpty()) { out = a; return; }
    acc.result(out);
}

inline void storeColour(uint16_t *bgra, const float rgb[3], const SrgbTables &t)
{
    bgra[2] = t.encode16(rgb[0]);
    bgra[1] = t.encode16(rgb[1]);
    bgra[0] = t.encode16(rgb[2]);
}

inline uint16_t toU16(float a) { return uint16_t(std::clamp(a, 0.0f, 1.0f) * 65535.0f + 0.5f); }
inline uint16_t linearToU16(float v) { return toU16(v); }

/// linear BGRA16 -> linear RGB floats
inline void u16ToLinear(const uint16_t *bgra, float rgb[3])
{
    rgb[0] = bgra[2] * inv65535;
    rgb[1] = bgra[1] * inv65535;
    rgb[2] = bgra[0] * inv65535;
}

inline uint64_t packBgr(const uint16_t *bgra)
{
    return uint64_t(bgra[0]) | (uint64_t(bgra[1]) << 16) | (uint64_t(bgra[2]) << 32);
}

} // namespace

KisWetPaintEngine::KisWetPaintEngine()
{
    const float grey[3] = {0.5f, 0.5f, 0.5f};
    makePigment(grey, m_fg);
    m_reservoir = m_fg;
    std::fill(m_bristles, m_bristles + BristleCount, 1.0f);
    (void) SrgbTables::instance();
}

void KisWetPaintEngine::beginStroke(const float fgLinear[3], bool cleanBrush, uint32_t seed)
{
    makePigment(fgLinear, m_fg);

    if (cleanBrush || !m_hasReservoir) {
        m_reservoir = m_fg;
    } else {
        // dirty brush: keep the old paint, dipped in the new colour
        Pigment dipped;
        mixToPigment(m_reservoir, 0.35f, m_fg, 0.65f, dipped);
        m_reservoir = dipped;
    }
    m_load = 1.0f;
    m_hasReservoir = true;

    // a fresh bristle profile per stroke: smoothed value noise in [0.1, 1]
    uint32_t s = seed * 747796405u + 2891336453u;
    float raw[BristleCount];
    for (int i = 0; i < BristleCount; i++) {
        s = s * 1664525u + 1013904223u;
        raw[i] = float(s >> 8) / float(1u << 24);
    }
    for (int i = 0; i < BristleCount; i++) {
        const float l = raw[std::max(i - 1, 0)], c = raw[i], r = raw[std::min(i + 1, BristleCount - 1)];
        m_bristles[i] = 0.1f + 0.9f * (0.25f * l + 0.5f * c + 0.25f * r);
    }
}

void KisWetPaintEngine::setForeground(const float fgLinear[3])
{
    makePigment(fgLinear, m_fg);
}

void KisWetPaintEngine::setReservoir(const float linear[3])
{
    makePigment(linear, m_reservoir);
    m_hasReservoir = true;
}

void KisWetPaintEngine::pickup(const Dab &dab, const Params &params, float steps)
{
    if (params.pickup <= 0.0f) return;

        const int n = dab.width * dab.height;
    // cap the sample count; the colour under a brush converges fast
    const int step = std::max(1, int(std::sqrt(float(n) / 1024.0f)));

    Accumulator acc;
    PigmentHashCache<64> cache;
    float sumWet = 0.0f, sumMask = 0.0f;

    for (int y = 0; y < dab.height; y += step) {
        for (int x = 0; x < dab.width; x += step) {
            const int i = y * dab.width + x;
            const float m = dab.mask[i] * inv255;
            if (m <= 0.0f) continue;
            sumMask += m;
            const uint8_t wetness = dab.wetness[i];
            if (!wetness) continue;                       // dried: nothing to pick up
            const uint16_t *wp = dab.wetPaint + i * 4;
            const float w = m * wp[3] * inv65535 * (wetness * inv255);
            if (w <= 0.002f) continue;
            sumWet += w;
            acc.add(cache.get(packBgr(wp), [&](float *o) { u16ToLinear(wp, o); }), w);
        }
    }

    if (acc.isEmpty() || sumMask <= 0.0f) return;

    const float avgWet = sumWet / sumMask;   // how much wet paint is under the brush, 0..1
    // a brush full of paint has little room to soak up more: pickup mostly
    // happens as the brush empties (otherwise a new colour is muddied by the
    // wet paint under it within a few dabs and seems to go underneath)
    const float q = overSteps(params.pickup * avgWet * 0.12f * (1.0f - 0.6f * m_load), steps);
    if (q <= 0.0f) return;

    Pigment canvas;
    acc.result(canvas);
    Pigment mixed;
    mixToPigment(m_reservoir, 1.0f - q, canvas, q, mixed);
    m_reservoir = mixed;
}

static inline float dabSteps(const KisWetPaintEngine::Dab &dab)
{
    const float diameter = std::max(2.0f * dab.radius, 1.0f);
    return dab.travelled > 0.0f
            ? std::clamp(dab.travelled / (0.1f * diameter), 0.05f, 20.0f)
            : 1.0f;
}

KisWetPaintEngine::DabContext KisWetPaintEngine::prepareDab(const Dab &dab, const Params &params)
{
    const float steps = dabSteps(dab);

    // 1-2. pickup and reload
    pickup(dab, params, steps);

    if (params.reload > 0.0f) {
        const float r = overSteps(params.reload * 0.5f, steps);
        Pigment mixed;
        mixToPigment(m_reservoir, 1.0f - r, m_fg, r, mixed);
        m_reservoir = mixed;
        m_load += r * (1.0f - m_load);
    }

    DabContext ctx;
    ctx.smearRate = overSteps(params.smear * 0.6f, steps);
    // ~35% coverage per 10% of the diameter travelled: a dry pixel is fully
    // covered by one pass of the brush, a wet one keeps some of its old paint
    ctx.depositRate = overSteps(0.35f, steps);
    ctx.undertone = std::clamp(params.undertone, 0.0f, 1.0f);
    // hiding power -> scattering x thickness of the thickest layer. One pass of
    // a loaded brush gives a medium layer (undertone showing), repeated passes
    // build to the masstone.
    const float hide = std::clamp(params.hiding, 0.0f, 1.0f);
    ctx.hidingSX = 0.3f + 4.8f * hide * hide;           // 50% -> 1.5
    // with undertone on, paint builds up more slowly so thin layers exist
    // (80% -> ~0.28 of the normal deposit per dab)
    ctx.thicknessRate = 1.0f / (1.0f + 4.0f * ctx.undertone * ctx.undertone);
    // per-bristle loading only matters with the thickness model; without it
    // streaks come from the dry-brush threshold alone (unchanged behaviour)
    ctx.bristleVariation = ctx.undertone > 0.0f ? std::min(0.9f, 1.1f * std::clamp(params.streaks, 0.0f, 1.0f)) : 0.0f;
    ctx.glazeFloor = 0.012f;
#ifdef WETPAINT_TUNING
    extern float g_tuneSX, g_tuneRate, g_tuneBristle, g_tuneAlphaK;
    if (g_tuneSX > 0) ctx.hidingSX = g_tuneSX;
    if (g_tuneRate > 0) ctx.thicknessRate = g_tuneRate;
    if (g_tuneBristle >= 0) ctx.bristleVariation = g_tuneBristle;
    extern float g_tuneFloor;
    if (g_tuneAlphaK > 0) ctx.glazeEdgeSX = g_tuneAlphaK;
    if (g_tuneFloor > 0) ctx.glazeFloor = g_tuneFloor;
#endif
    // dry-brush streaks: a bristle profile across the stroke
    ctx.nx = -dab.dirY;
    ctx.ny = dab.dirX;
    ctx.invR = 1.0f / std::max(dab.radius, 1.0f);
    ctx.need = (1.0f - m_load) * params.streaks * 1.1f;
    ctx.loadFactor = std::min(1.0f, m_load * 3.0f);
    toLinear(m_reservoir, ctx.reservoirRgb);
    return ctx;
}

float KisWetPaintEngine::paintRows(const Dab &dab, const Params &params, const DabContext &ctx, int y0, int y1) const
{
    const SrgbTables &t = SrgbTables::instance();

    const float smearRate = ctx.smearRate;
    const float nx = ctx.nx, ny = ctx.ny, invR = ctx.invR;
    const float need = ctx.need;
    const float loadFactor = ctx.loadFactor;
    const float *reservoirRgb = ctx.reservoirRgb;

    // under / wetPaint buffers hold LINEAR 16-bit colour (private to this
    // engine), so reading and writing them is a multiply, not an sRGB curve.
    // Only the visible layer pixel is sRGB.
    PigmentHashCache<64> filmCache, srcCache, underCache;
    float used = 0.0f;

    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < dab.width; x++) {
            const int i = y * dab.width + x;
            const float m = dab.mask[i] * inv255;
            if (m <= 0.0f) continue;

            uint16_t *px = dab.pixels + i * 4;
            uint16_t *under = dab.under + i * 4;
            uint16_t *wp = dab.wetPaint + i * 4;
            uint8_t &wetnessRef = dab.wetness[i];

            // lazy bake: dried (or never painted) pixels start a new wet film
            // over whatever the layer shows now
            if (!wetnessRef) {
                under[0] = linearToU16(t.decode16[px[0]]);
                under[1] = linearToU16(t.decode16[px[1]]);
                under[2] = linearToU16(t.decode16[px[2]]);
                under[3] = px[3];
                wp[0] = wp[1] = wp[2] = wp[3] = 0;
            }

            float aw = wp[3] * inv65535;
            float wet = wetnessRef * inv255;

            // the wet film as a pigment, built only if something mixes into it
            Pigment film;
            float filmRgb[3];
            bool haveFilm = false;
            bool changed = false;

            // 2b. rewet: lift some of the dry paint into the wet film, every
            // dab, so the brush works into existing paint the longer it is over
            // it (a soft edge alone would only ever dampen it a little). Film
            // and dry paint start as the same colour, so nothing visibly changes
            // until the paint is actually moved.
            if (params.rewet > 0.0f) {
                const float rw = params.rewet * m;
                const float au = under[3] * inv65535;
                if (rw > 0.002f && au > 0.002f && aw < 0.999f) {
                    const float total = aw + au * (1.0f - aw);
                    const float lifted = rw * au * (1.0f - aw);
                    const Pigment &up = underCache.get(packBgr(under), [&](float *o) { u16ToLinear(under, o); });
                    if (aw <= 0.001f) {
                        film = up;
                        u16ToLinear(under, filmRgb);
                    } else {
                        const Pigment cur = filmCache.get(packBgr(wp), [&](float *o) { u16ToLinear(wp, o); });
                        mixPigments(cur, up, lifted / (aw + lifted), film, filmRgb);
                    }
                    aw += lifted;
                    under[3] = toU16(aw < 0.999f ? std::max(0.0f, (total - aw) / (1.0f - aw)) : 0.0f);
                    wet = std::max(wet, rw);
                    haveFilm = true;
                    changed = true;
                }
            }

            // 3. smear the wet film from the previous dab position
            if (dab.smearWetPaint && smearRate > 0.0f) {
                const uint16_t *sp = dab.smearWetPaint + i * 4;
                const float sWet = dab.smearWetness ? dab.smearWetness[i] * inv255 : 0.0f;
                const float sAw = sp[3] * inv65535;
                const float s = smearRate * m * sWet;
                if (s > 0.002f && sAw > 0.0f) {
                    const float srcAmount = s * sAw;
                    const float dstAmount = (1.0f - s) * aw;
                    const Pigment &src = srcCache.get(packBgr(sp), [&](float *o) { u16ToLinear(sp, o); });
                    if (aw <= 0.0f || dstAmount <= 0.0f) {
                        film = src;
                        u16ToLinear(sp, filmRgb);
                    } else {
                        const Pigment cur = haveFilm ? film
                                                     : filmCache.get(packBgr(wp), [&](float *o) { u16ToLinear(wp, o); });
                        mixPigments(cur, src, srcAmount / (srcAmount + dstAmount), film, filmRgb);
                    }
                    haveFilm = true;
                    aw = aw + (sAw - aw) * s;
                    wet = wet + (sWet - wet) * s;
                    changed = true;
                }
            }

            // 4. deposit into the wet film
            // Bristles: each carries a different amount of paint, so even a
            // loaded brush lays thicker and thinner streaks (which the
            // undertone model turns into darker / brighter streaks). As the
            // brush empties the weak bristles stop depositing altogether.
            float bristle = 1.0f;
            if (need > 0.0f || ctx.bristleVariation > 0.0f) {
                const float u = ((x + 0.5f - dab.centreX) * nx + (y + 0.5f - dab.centreY) * ny) * invR;
                const float f = std::clamp(u * 0.5f + 0.5f, 0.0f, 1.0f) * (BristleCount - 1);
                const int b0 = int(f);
                const int b1 = std::min(b0 + 1, BristleCount - 1);
                const float bv = m_bristles[b0] + (m_bristles[b1] - m_bristles[b0]) * (f - b0);
                bristle = 1.0f - ctx.bristleVariation * (1.0f - bv);
                if (need > 0.0f) bristle *= smoothstep(need - 0.12f, need + 0.12f, bv);
            }

            const float amount = m * params.opacity * bristle * loadFactor;
            // c = how much of what you SEE becomes the new paint (fresh paint from
            // a loaded brush lands on top of the wet film); d = how much the film
            // THICKENS. The thickness model slows d down, but must not slow c, or
            // a new colour stroked over wet paint would vanish into it, looking
            // as if it went underneath.
            const float c = amount * ctx.depositRate * (1.0f - params.wetMixing * aw * wet);
            const float d = c * ctx.thicknessRate;

            if (d > 0.001f) {
                if (aw <= 0.0f) {
                    film = m_reservoir;
                    filmRgb[0] = reservoirRgb[0]; filmRgb[1] = reservoirRgb[1]; filmRgb[2] = reservoirRgb[2];
                    aw = d;
                } else {
                    if (!haveFilm) {
                        film = filmCache.get(packBgr(wp), [&](float *o) { u16ToLinear(wp, o); });
                    }
                    // Undertone on: the film alpha is paint THICKNESS and simply
                    // accumulates, so thick and thin bristle streaks stay
                    // distinct. Off: it is coverage, which saturates.
                    const float newAw = ctx.undertone > 0.0f ? std::min(1.0f, aw + d) : d + aw * (1.0f - d);
                    Pigment mixed;
                    mixPigments(film, m_reservoir, c / (c + aw * (1.0f - c)), mixed, filmRgb);
                    film = mixed;
                    aw = newAw;
                }
                haveFilm = true;
                wet = wet + (params.paintWetness - wet) * amount;
                used += amount;
                changed = true;
            }

            if (!changed) continue;

            // store the film (linear)
            wp[2] = linearToU16(filmRgb[0]);
            wp[1] = linearToU16(filmRgb[1]);
            wp[0] = linearToU16(filmRgb[2]);
            wp[3] = toU16(aw);
            // keep pixels that are still wet from being baked next time
            const uint8_t w8 = uint8_t(std::clamp(wet, 0.0f, 1.0f) * 255.0f + 0.5f);
            wetnessRef = (aw > 0.0f && w8 == 0) ? 1 : w8;

            // the layer pixel = wet film over the dry paint
            const float au = under[3] * inv65535;
            if (ctx.undertone > 0.0f) {
                if (!haveFilm) {
                    film = filmCache.get(packBgr(wp), [&](float *o) { u16ToLinear(wp, o); });
                }
                // Kubelka-Munk layer of finite thickness: thin paint lets light
                // reach the ground and come back through the pigment, showing
                // its brighter, more saturated undertone; thick paint shows the
                // masstone. Thickness grows faster than linearly with amount.
                const float SX = ctx.hidingSX * aw;
                if (au <= 0.0f) {
                    // empty layer: model it over white paper, then turn "what
                    // it looks like over white" into colour + alpha, so it
                    // still composites onto lower layers as a translucent glaze
                    float g[3], cov = 0.0f;
                    glaze(film, nullptr, SX, g, &cov, ctx.glazeFloor);
                    float d[3];
                    for (int c = 0; c < 3; c++) {
                        const float fade = filmRgb[c] * aw + (1.0f - aw);      // plain transparency
                        const float lin = fade + (g[c] - fade) * ctx.undertone;
                        d[c] = t.encode16(lin) * inv65535;                    // composite space (sRGB)
                    }
                    const float minD = std::min(d[0], std::min(d[1], d[2]));
                    // smallest alpha that reproduces it, but never less than the
                    // paint actually there (or white paint would vanish)
                    // Alpha: just enough to reproduce the colour over white, but
                    // limited by how much the layer actually hides the paper, so
                    // thin edges stay soft instead of turning into a hard rim.
                    // Never below the paint amount, or white paint would vanish.
                    (void) cov;
                    const float edge = smoothstep(0.0f, ctx.glazeEdgeSX, SX);
                    const float a = std::clamp(std::max((1.0f - minD) * edge, aw * (1.0f - ctx.undertone)), 0.0f, 1.0f);
                    if (a > 0.0f) {
                        for (int c = 0; c < 3; c++) {
                            const float col = std::clamp(1.0f - (1.0f - d[c]) / a, 0.0f, 1.0f);
                            px[2 - c] = uint16_t(col * 65535.0f + 0.5f);
                        }
                    }
                    px[3] = toU16(a);
                } else {
                    // paint on paint: the real ground is the dry paint below
                    const Pigment &up = underCache.get(packBgr(under), [&](float *o) { u16ToLinear(under, o); });
                    float g[3], m2[3];
                    glaze(film, &up, SX, g, nullptr, ctx.glazeFloor);
                    const float aOut = aw + au * (1.0f - aw);
                    if (ctx.undertone < 1.0f) {
                        mix(up, film, aw / aOut, m2);
                        for (int c = 0; c < 3; c++) g[c] = m2[c] + (g[c] - m2[c]) * ctx.undertone;
                    }
                    storeColour(px, g, t);
                    px[3] = toU16(aOut);
                }
            } else if (aw >= 0.999f || au <= 0.0f) {
                storeColour(px, filmRgb, t);
                px[3] = toU16(aw >= 0.999f ? 1.0f : aw);
            } else {
                const float aOut = aw + au * (1.0f - aw);
                const Pigment &up = underCache.get(packBgr(under), [&](float *o) { u16ToLinear(under, o); });
                float rgb[3];
                mix(up, film, aw / aOut, rgb);
                storeColour(px, rgb, t);
                px[3] = toU16(aOut);
            }
        }
    }

    return used;
}

void KisWetPaintEngine::finishDab(const Dab &dab, const Params &params, float used)
{
    // 5. deplete, by average coverage times the distance travelled in diameters
    const float diameter = std::max(2.0f * dab.radius, 1.0f);
    const float area = 3.14159265f * dab.radius * dab.radius;
    const float coverage = std::min(1.0f, used / std::max(area, 1.0f));
    const float diameters = dab.travelled / diameter;
    m_load = std::max(0.0f, m_load - params.depletion * coverage * diameters * 0.25f);
}

void KisWetPaintEngine::paintDab(const Dab &dab, const Params &params)
{
    if (dab.width <= 0 || dab.height <= 0) return;
    const DabContext ctx = prepareDab(dab, params);
    const float used = paintRows(dab, params, ctx, 0, dab.height);
    finishDab(dab, params, used);
}
