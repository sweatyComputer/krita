/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KOCOMPOSITEOP_PIGMENT_H
#define KOCOMPOSITEOP_PIGMENT_H

#include <type_traits>

#include <KoCompositeOp.h>
#include <KoCompositeOpRegistry.h>
#include <KoColorSpace.h>
#include <KoColorProfile.h>
#include <KoColorSpaceMaths.h>

#include "KoPigmentMixer.h"

/**
 * "Pigment" blending mode: mixes the source into the destination the way paint
 * mixes (Kubelka-Munk, see KoPigmentMixer.h) instead of the way light mixes.
 * Blue over yellow gives green, colours darken and saturate as they mix, and a
 * weak tint into white keeps its hue.
 *
 * Works on every RGB(A) colour space: integer spaces are assumed to be stored
 * sRGB-encoded and float spaces linear, unless the profile says otherwise.
 *
 * Alpha behaves like Normal: the amount of source paint is
 * mask * opacity * srcAlpha, the amount already there is dstAlpha * (1 - that),
 * and those two amounts are the KM mixing weights.
 *
 * Two caches make it cheap where it matters: a constant source colour (every
 * plain brush dab) is converted to a pigment once per call, and runs of equal
 * destination pixels reuse the last destination pigment.
 */
template<class Traits>
class KoCompositeOpPigment : public KoCompositeOp
{
    typedef typename Traits::channels_type channels_type;
    static const qint32 channels_nb = Traits::channels_nb;
    static const qint32 alpha_pos = Traits::alpha_pos;
    static const qint32 red_pos = Traits::red_pos;
    static const qint32 green_pos = Traits::green_pos;
    static const qint32 blue_pos = Traits::blue_pos;

    static constexpr bool isU8 = std::is_same<channels_type, quint8>::value;
    static constexpr bool isU16 = std::is_same<channels_type, quint16>::value;

public:
    KoCompositeOpPigment(const KoColorSpace *cs)
        : KoCompositeOp(cs, COMPOSITE_PIGMENT, KoCompositeOp::categoryMix())
        , m_linear(!(isU8 || isU16))
    {
        const KoColorProfile *profile = cs ? cs->profile() : nullptr;
        if (profile) {
            m_linear = profile->isLinear();
        }
        // make sure the tables exist before any painting thread needs them
        (void) KoPigment::SrgbTables::instance();
    }

    using KoCompositeOp::composite;

    void composite(const KoCompositeOp::ParameterInfo &params) const override
    {
        const bool allChannelFlags = params.channelFlags.isEmpty()
                || params.channelFlags == QBitArray(channels_nb, true);
        const bool alphaLocked = !params.channelFlags.isEmpty()
                && !params.channelFlags.testBit(alpha_pos);

        if (allChannelFlags) {
            if (params.maskRowStart) genericComposite<true, false, true>(params);
            else                     genericComposite<false, false, true>(params);
        } else if (alphaLocked) {
            if (params.maskRowStart) genericComposite<true, true, false>(params);
            else                     genericComposite<false, true, false>(params);
        } else {
            if (params.maskRowStart) genericComposite<true, false, false>(params);
            else                     genericComposite<false, false, false>(params);
        }
    }

private:
    inline float toLinear(channels_type v) const
    {
        const KoPigment::SrgbTables &t = KoPigment::SrgbTables::instance();
        if constexpr (isU8) {
            return m_linear ? v * (1.0f / 255.0f) : t.decode8[v];
        } else if constexpr (isU16) {
            return m_linear ? v * (1.0f / 65535.0f) : t.decode16[v];
        } else {
            const float f = KoColorSpaceMaths<channels_type, float>::scaleToA(v);
            return m_linear ? f : KoPigment::SrgbTables::toLin(qBound(0.0f, f, 1.0f));
        }
    }

    inline channels_type fromLinear(float v) const
    {
        const KoPigment::SrgbTables &t = KoPigment::SrgbTables::instance();
        if constexpr (isU8) {
            return m_linear ? channels_type(qBound(0.0f, v, 1.0f) * 255.0f + 0.5f) : t.encode8(v);
        } else if constexpr (isU16) {
            return m_linear ? channels_type(qBound(0.0f, v, 1.0f) * 65535.0f + 0.5f) : t.encode16(v);
        } else {
            if (!m_linear) {
                v = float(t.encode16(v)) * (1.0f / 65535.0f);
            }
            return KoColorSpaceMaths<float, channels_type>::scaleToA(v);
        }
    }

    inline void pixelToPigment(const channels_type *px, KoPigment::Pigment &p) const
    {
        const float rgb[3] = {toLinear(px[red_pos]), toLinear(px[green_pos]), toLinear(px[blue_pos])};
        KoPigment::makePigment(rgb, p);
    }

    template<bool useMask, bool alphaLocked, bool allChannelFlags>
    void genericComposite(const KoCompositeOp::ParameterInfo &params) const
    {
        const QBitArray &flags = params.channelFlags;
        const qint32 srcInc = (params.srcRowStride == 0) ? 0 : channels_nb;
        const float opacity = params.opacity;

        quint8 *dstRowStart = params.dstRowStart;
        const quint8 *srcRowStart = params.srcRowStart;
        const quint8 *maskRowStart = params.maskRowStart;

        // caches: the last source / destination colour seen and its pigment
        KoPigment::Pigment srcPigment, dstPigment;
        // integer pixels: a small hash cache of destination pigments - painted
        // areas repeat colours a lot. Float pixels keep the exact last-colour check.
        KoPigment::PigmentHashCache<64> dstCache;
        quint64 lastDstKey = ~quint64(0);
        const KoPigment::Pigment *lastDstP = nullptr;

        // Result memo for the common brush-dab case: one constant source colour
        // painted through a mask. Over an unchanged destination pixel value the
        // result then depends only on the mask byte, so remember up to 256
        // results per destination value. Painting over flat areas becomes a lookup.
        constexpr bool memoTypeOk = isU8 || isU16;
        const bool memoOn = memoTypeOk && srcInc == 0;
        channels_type memo[256][3];
        quint32 memoStamp[256] = {};
        quint32 memoGen = 0;
        quint64 memoKey = 0;
        bool memoKeyValid = false;
        channels_type lastSrc[3] = {0, 0, 0}, lastDst[3] = {0, 0, 0};
        bool haveSrc = false, haveDst = false;

        for (qint32 r = 0; r < params.rows; ++r) {
            const channels_type *src = reinterpret_cast<const channels_type *>(srcRowStart);
            channels_type *dst = reinterpret_cast<channels_type *>(dstRowStart);
            const quint8 *mask = maskRowStart;

            for (qint32 c = 0; c < params.cols; ++c) {
                const float srcAlpha = KoColorSpaceMaths<channels_type, float>::scaleToA(src[alpha_pos]);
                const float dstAlpha = KoColorSpaceMaths<channels_type, float>::scaleToA(dst[alpha_pos]);
                const float maskAlpha = useMask ? (*mask) * (1.0f / 255.0f) : 1.0f;

                // float spaces can carry alpha > 1; paint amounts can't
                const float applied = qBound(0.0f, maskAlpha * srcAlpha * opacity, 1.0f);

                if (applied > 0.0f) {
                    if (dstAlpha <= 0.0f) {
                        // nothing to mix with: plain copy of the colour
                        if (!alphaLocked) {
                            for (int ch = 0; ch < channels_nb; ch++) {
                                if (ch == alpha_pos) continue;
                                if (allChannelFlags || flags.testBit(ch)) dst[ch] = src[ch];
                            }
                            dst[alpha_pos] = KoColorSpaceMaths<float, channels_type>::scaleToA(applied);
                        }
                    } else {
                        const float newAlpha = alphaLocked ? dstAlpha : applied + dstAlpha * (1.0f - applied);
                        // share of new paint in the mix, by amount
                        const float t = alphaLocked ? applied
                                                    : qBound(0.0f, applied / qMax(newAlpha, 1e-6f), 1.0f);

                        const int memoIdx = useMask ? int(*mask) : 255;
                        channels_type outC[3];
                        bool haveOut = false;
                        if constexpr (memoTypeOk) {
                            if (memoOn) {
                                const quint64 dk = quint64(dst[0]) | (quint64(dst[1]) << 16)
                                                 | (quint64(dst[2]) << 32) | (quint64(dst[3]) << 48);
                                if (!memoKeyValid || dk != memoKey) {
                                    memoKey = dk;
                                    memoKeyValid = true;
                                    ++memoGen;
                                }
                                if (memoStamp[memoIdx] == memoGen) {
                                    outC[0] = memo[memoIdx][0]; outC[1] = memo[memoIdx][1]; outC[2] = memo[memoIdx][2];
                                    haveOut = true;
                                }
                            }
                        }

                        if (!haveOut) {
                            if (!haveSrc || src[red_pos] != lastSrc[0] || src[green_pos] != lastSrc[1] || src[blue_pos] != lastSrc[2]) {
                                pixelToPigment(src, srcPigment);
                                lastSrc[0] = src[red_pos]; lastSrc[1] = src[green_pos]; lastSrc[2] = src[blue_pos];
                                haveSrc = true;
                            }
                            const KoPigment::Pigment *dstP = &dstPigment;
                            if constexpr (isU8 || isU16) {
                                const quint64 key = quint64(dst[red_pos]) | (quint64(dst[green_pos]) << 16) | (quint64(dst[blue_pos]) << 32);
                                if (key != lastDstKey) {   // runs of one colour skip the hash entirely
                                    lastDstP = &dstCache.get(key, [&](float *rgb) {
                                        rgb[0] = toLinear(dst[red_pos]);
                                        rgb[1] = toLinear(dst[green_pos]);
                                        rgb[2] = toLinear(dst[blue_pos]);
                                    });
                                    lastDstKey = key;
                                }
                                dstP = lastDstP;
                            } else if (!haveDst || dst[red_pos] != lastDst[0] || dst[green_pos] != lastDst[1] || dst[blue_pos] != lastDst[2]) {
                                pixelToPigment(dst, dstPigment);
                                lastDst[0] = dst[red_pos]; lastDst[1] = dst[green_pos]; lastDst[2] = dst[blue_pos];
                                haveDst = true;
                            }

                            float out[3];
                            KoPigment::mix(*dstP, srcPigment, t, out);
                            outC[0] = fromLinear(out[0]);
                            outC[1] = fromLinear(out[1]);
                            outC[2] = fromLinear(out[2]);
                            if (memoOn) {
                                memo[memoIdx][0] = outC[0]; memo[memoIdx][1] = outC[1]; memo[memoIdx][2] = outC[2];
                                memoStamp[memoIdx] = memoGen;
                            }
                        }

                        if (allChannelFlags || flags.testBit(red_pos))   dst[red_pos]   = outC[0];
                        if (allChannelFlags || flags.testBit(green_pos)) dst[green_pos] = outC[1];
                        if (allChannelFlags || flags.testBit(blue_pos))  dst[blue_pos]  = outC[2];

                        if (!alphaLocked) {
                            dst[alpha_pos] = KoColorSpaceMaths<float, channels_type>::scaleToA(newAlpha);
                        }
                    }
                }

                src += srcInc;
                dst += channels_nb;
                if (useMask) ++mask;
            }

            srcRowStart += params.srcRowStride;
            dstRowStart += params.dstRowStride;
            maskRowStart += params.maskRowStride;
        }
    }

    bool m_linear;
};

#endif // KOCOMPOSITEOP_PIGMENT_H
