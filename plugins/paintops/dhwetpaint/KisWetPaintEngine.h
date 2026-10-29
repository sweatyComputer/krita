/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_WET_PAINT_ENGINE_H
#define KIS_WET_PAINT_ENGINE_H

#include <cstdint>
#include "KoPigmentMixer.h"

/**
 * The wet-paint simulation, free of any Krita types so it can be unit tested
 * and benchmarked on its own. KisWetPaintOp feeds it buffers.
 *
 * TWO-LAYER CANVAS MODEL
 *
 * Every pixel the brush has touched is described by
 *   - under   : the dry paint underneath (BGRA16)
 *   - wet     : the wet paint film on top (BGRA16, alpha = amount of wet paint)
 *   - wetness : how wet that film still is (8-bit, 0 = dried)
 *   - pixel   : what the layer shows = wet film KM-mixed over the dry paint by amount
 *
 * Only the wet film can be picked up or smeared. That is what makes painting
 * into wet paint mix (wet-in-wet) while painting over dried paint layers
 * cleanly on top (wet-on-dry), and it stops a brush from "picking up" the
 * optical mix of its own fresh paint with the dry paint below.
 *
 * Dried pixels are baked lazily: the first time a dab touches a pixel whose
 * wetness is 0, its current layer colour becomes the new dry "under" and the
 * wet film starts empty. So drying is just letting wetness decay.
 *
 * PER DAB
 *  1. PICKUP   sample the wet film under the brush (mask * amount * wetness)
 *              and KM-mix it into the brush reservoir.
 *  2. RELOAD   mix fresh foreground paint back in (0 = brush runs dry and
 *              turns into a blender).
 *  3. SMEAR    drag the wet film from the previous dab position.
 *  4. DEPOSIT  lay the reservoir colour into the wet film.
 *  5. DEPLETE  lose paint by coverage x distance; a per-stroke bristle
 *              profile thresholds the deposit as the brush empties
 *              -> dry-brush streaks.
 *
 * All per-dab rates are defined per 10% of the brush diameter travelled, so
 * the result does not depend on the spacing setting.
 */
class KisWetPaintEngine
{
public:
    struct Params {
        float opacity = 1.0f;       ///< deposit strength (Opacity option)
        float pickup = 0.5f;        ///< how much wet canvas paint the brush picks up
        float smear = 0.5f;         ///< how strongly wet paint is dragged
        float reload = 0.15f;       ///< fresh paint mixed back in
        float depletion = 0.5f;     ///< how fast the brush runs out (0 = never)
        float streaks = 0.6f;       ///< dry-brush bristle streak strength
        float wetMixing = 0.6f;     ///< how much existing wet paint dilutes new paint
        float paintWetness = 1.0f;  ///< wetness of freshly deposited paint
        float rewet = 0.0f;         ///< how much existing dry paint the brush re-wets (blenders)
        float undertone = 0.8f;     ///< 0 = thin paint just fades out, 1 = full KM glazing
        float hiding = 0.5f;        ///< hiding power: how quickly paint reaches its masstone
    };

    struct Dab {
        int width = 0;
        int height = 0;
        uint16_t *pixels = nullptr;            ///< in/out: the layer, BGRA16 sRGB
        uint16_t *under = nullptr;             ///< in/out: dry paint, BGRA16 LINEAR (engine-private)
        uint16_t *wetPaint = nullptr;          ///< in/out: wet film, BGRA16 LINEAR (alpha = amount)
        uint8_t *wetness = nullptr;            ///< in/out
        const uint8_t *mask = nullptr;         ///< brush dab (already includes selection)
        const uint16_t *smearWetPaint = nullptr; ///< wet film at the previous dab position (linear), or null
        const uint8_t *smearWetness = nullptr;   ///< wetness at the previous dab position, or null
        float centreX = 0, centreY = 0;        ///< dab centre in buffer coordinates
        float dirX = 1, dirY = 0;              ///< unit stroke direction
        float radius = 1;                      ///< pixels
        float travelled = 0;                   ///< distance since the previous dab, pixels (0 = first dab)
    };

    KisWetPaintEngine();

    /// start a stroke; a clean brush is fully reloaded with \p fgLinear
    void beginStroke(const float fgLinear[3], bool cleanBrush, uint32_t seed);

    /// change the paint colour mid-stroke (colour dynamics); does not reload
    void setForeground(const float fgLinear[3]);

    /// seed the reservoir with paint left over from a previous stroke (dirty brush)
    void setReservoir(const float linear[3]);

    /// per-dab values shared by all rows
    struct DabContext {
        float smearRate = 0;
        float depositRate = 0.35f;
        float undertone = 0.0f;
        float hidingSX = 1.1f;
        float thicknessRate = 1.0f;
        float bristleVariation = 0.0f;
        float glazeEdgeSX = 0.06f;
        float glazeFloor = 0.012f;
        float nx = 0, ny = 0, invR = 1;
        float need = 0;
        float loadFactor = 1;
        float reservoirRgb[3] = {0, 0, 0};
    };

    /**
     * Painting one dab is three phases so the middle one can run in parallel:
     *   prepareDab()  pickup + reload, updates the brush (not thread safe)
     *   paintRows()   rows [y0, y1) — const, safe to run concurrently on
     *                 disjoint row ranges; returns the paint deposited
     *   finishDab()   depletion (not thread safe)
     */
    DabContext prepareDab(const Dab &dab, const Params &params);
    float paintRows(const Dab &dab, const Params &params, const DabContext &ctx, int y0, int y1) const;
    void finishDab(const Dab &dab, const Params &params, float used);

    /// all three phases on the calling thread
    void paintDab(const Dab &dab, const Params &params);

    float load() const { return m_load; }
    void reservoirLinear(float out[3]) const { KoPigment::toLinear(m_reservoir, out); }

private:
    void pickup(const Dab &dab, const Params &params, float steps);

    KoPigment::Pigment m_fg;
    KoPigment::Pigment m_reservoir;
    float m_load = 1.0f;
    bool m_hasReservoir = false;

    static constexpr int BristleCount = 64;
    float m_bristles[BristleCount];
};

#endif // KIS_WET_PAINT_ENGINE_H
