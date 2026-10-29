/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintInterstrokeData.h"

#include <QDateTime>
#include <QtMath>
#include <QtGlobal>

#include <KoColorSpaceRegistry.h>
#include <kundo2command.h>

#include "kis_paint_device.h"
#include "kis_transaction.h"
#include "kis_sequential_iterator.h"

namespace {
KisPaintDeviceSP createSideDevice(KisPaintDeviceSP source, const KoColorSpace *cs)
{
    KisPaintDeviceSP dev = new KisPaintDevice(cs);
    dev->setDefaultBounds(source->defaultBounds());
    dev->setSupportsWraparoundMode(source->supportsWraproundMode());
    return dev;
}
}

KisWetPaintInterstrokeData::KisWetPaintInterstrokeData(KisPaintDeviceSP source)
    : KisInterstrokeData(source)
{
    const KoColorSpace *rgb16 = KoColorSpaceRegistry::instance()->rgb16();
    underDevice = createSideDevice(source, rgb16);
    wetPaintDevice = createSideDevice(source, rgb16);
    wetnessDevice = createSideDevice(source, KoColorSpaceRegistry::instance()->alpha8());
    lastUpdateMSecs = QDateTime::currentMSecsSinceEpoch();
}

KisWetPaintInterstrokeData::~KisWetPaintInterstrokeData()
{
    if (m_parentCommand) {
        (void) endTransaction();
    }
}

void KisWetPaintInterstrokeData::beginTransaction()
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(!m_parentCommand);

    m_parentCommand.reset(new KUndo2Command());
    m_underTransaction.reset(new KisTransaction(underDevice, m_parentCommand.get()));
    m_wetPaintTransaction.reset(new KisTransaction(wetPaintDevice, m_parentCommand.get()));
    m_wetnessTransaction.reset(new KisTransaction(wetnessDevice, m_parentCommand.get()));
}

KUndo2Command *KisWetPaintInterstrokeData::endTransaction()
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(m_parentCommand, nullptr);

    // the per-device commands are children of m_parentCommand
    (void) m_underTransaction->endAndTake();
    (void) m_wetPaintTransaction->endAndTake();
    (void) m_wetnessTransaction->endAndTake();
    m_underTransaction.reset();
    m_wetPaintTransaction.reset();
    m_wetnessTransaction.reset();

    return m_parentCommand.release();
}

void KisWetPaintInterstrokeData::applyDrying(qreal dryingSeconds)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qreal elapsed = qMax<qint64>(0, now - lastUpdateMSecs) / 1000.0;

    if (dryingSeconds <= 0.0) {
        lastUpdateMSecs = now;
        return;
    }

    const qreal keep = qExp(-elapsed / dryingSeconds);

    // Quick successive strokes: let the time accumulate until there is a
    // step worth applying, otherwise 8-bit rounding would either dry the
    // paint far too fast or never at all.
    if (keep > 0.985) return;
    lastUpdateMSecs = now;

    const QRect rc = wetnessDevice->exactBounds();
    if (rc.isEmpty()) return;

    if (keep < 1.0 / 255.0) {
        // everything has dried: the layer pixels are the paint now
        wetnessDevice->clear();
        return;
    }

    // exponential decay; anything that rounds to 0 is dry and gets baked lazily
    quint8 lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = quint8(qFloor(i * keep)); // floor: always makes progress, even at 1
    }

    KisSequentialIterator it(wetnessDevice, rc);
    while (it.nextPixel()) {
        quint8 *p = it.rawData();
        *p = lut[*p];
    }
}
