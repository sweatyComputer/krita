/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisWetPaintOptionWidget.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include <klocalizedstring.h>
#include <kis_slider_spin_box.h>
#include <KisWidgetConnectionUtils.h>

#include "KisWetPaintOptionModel.h"

struct KisWetPaintOptionWidget::Private
{
    Private(lager::cursor<KisWetPaintOptionData> optionData)
        : model(optionData)
    {
    }

    KisWetPaintOptionModel model;
};

namespace {
KisDoubleSliderSpinBox *percentSlider(QWidget *parent)
{
    KisDoubleSliderSpinBox *s = new KisDoubleSliderSpinBox(parent);
    s->setRange(0.0, 100.0, 0);
    s->setSuffix(i18n("%"));
    return s;
}
}

KisWetPaintOptionWidget::KisWetPaintOptionWidget(lager::cursor<KisWetPaintOptionData> optionData)
    : KisPaintOpOption(i18n("Wet Paint"), KisPaintOpOption::GENERAL, true)
    , m_d(new Private(optionData))
{
    using namespace KisWidgetConnectionUtils;

    setObjectName("KisWetPaintOptionWidget");
    m_checkable = false;

    QWidget *page = new QWidget();

    KisDoubleSliderSpinBox *sldDrying = new KisDoubleSliderSpinBox(page);
    sldDrying->setRange(0.0, 600.0, 0);
    sldDrying->setSoftRange(0.0, 180.0);
    sldDrying->setSuffix(i18n(" s"));
    sldDrying->setSpecialValueText(i18nc("paint never dries", "Never"));
    sldDrying->setToolTip(i18n("Seconds for wet paint to mostly dry. Dry paint is no longer picked up or smeared, "
                               "and new paint layers over it instead of mixing. 0 = never dries."));

    KisDoubleSliderSpinBox *sldDepletion = percentSlider(page);
    sldDepletion->setToolTip(i18n("How quickly the brush runs out of paint. 0% = endless paint."));

    KisDoubleSliderSpinBox *sldStreaks = percentSlider(page);
    sldStreaks->setToolTip(i18n("How strongly a brush that's running dry breaks up into bristle streaks."));

    KisDoubleSliderSpinBox *sldWetMixing = percentSlider(page);
    sldWetMixing->setToolTip(i18n("How much wet paint on the canvas dilutes the paint you put into it. "
                                  "High = wet-in-wet blends, low = new paint covers."));

    KisDoubleSliderSpinBox *sldPaintWetness = percentSlider(page);
    sldPaintWetness->setToolTip(i18n("How wet freshly laid paint is."));

    KisDoubleSliderSpinBox *sldRewet = percentSlider(page);
    sldRewet->setToolTip(i18n("Re-wets paint that is already on the layer - dried wet paint, normal brush work, "
                              "imported images - so this brush can push and mix it. Use it for blenders. "
                              "0% = only fresh wet paint moves."));

    KisDoubleSliderSpinBox *sldUndertone = percentSlider(page);
    sldUndertone->setToolTip(i18n("Thin paint shows its undertone, like a real glaze: a thin blue goes brighter "
                                  "and more saturated and lets the colour below through, thick blue goes deep. "
                                  "0% = thin paint just fades out (old behaviour)."));

    KisDoubleSliderSpinBox *sldHiding = percentSlider(page);
    sldHiding->setToolTip(i18n("Hiding power (opacity of the pigment). Low = transparent paint that needs many "
                               "layers to reach its full colour, high = opaque paint that covers in one pass. "
                               "Only used when Undertone is above 0%."));

    QCheckBox *chkClean = new QCheckBox(i18n("Clean brush every stroke"), page);
    chkClean->setToolTip(i18n("When off, the brush keeps the paint it picked up from one stroke to the next."));

    QLabel *note = new QLabel(i18n("Pickup, Smear and Reload are pressure curves further down the list. "
                                   "Mixing is pigment based (Kubelka-Munk): blue and yellow make green."), page);
    note->setWordWrap(true);

    QFormLayout *form = new QFormLayout();
    form->addRow(i18n("Drying time:"), sldDrying);
    form->addRow(i18n("Paint depletion:"), sldDepletion);
    form->addRow(i18n("Dry-brush streaks:"), sldStreaks);
    form->addRow(i18n("Wet mixing:"), sldWetMixing);
    form->addRow(i18n("Paint wetness:"), sldPaintWetness);
    form->addRow(i18n("Rewet canvas:"), sldRewet);
    form->addRow(i18n("Undertone:"), sldUndertone);
    form->addRow(i18n("Hiding power:"), sldHiding);
    form->addRow(chkClean);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addStretch(1);

    connectControl(sldDrying, &m_d->model, "dryingTime");
    connectControl(sldDepletion, &m_d->model, "depletion");
    connectControl(sldStreaks, &m_d->model, "streaks");
    connectControl(sldWetMixing, &m_d->model, "wetMixing");
    connectControl(sldPaintWetness, &m_d->model, "paintWetness");
    connectControl(chkClean, &m_d->model, "cleanBrush");
    connectControl(sldRewet, &m_d->model, "rewet");
    connectControl(sldUndertone, &m_d->model, "undertone");
    connectControl(sldHiding, &m_d->model, "hiding");

    m_d->model.optionData.bind(std::bind(&KisWetPaintOptionWidget::emitSettingChanged, this));

    setConfigurationPage(page);
}

KisWetPaintOptionWidget::~KisWetPaintOptionWidget()
{
}

void KisWetPaintOptionWidget::writeOptionSetting(KisPropertiesConfigurationSP setting) const
{
    KisWetPaintOptionData data = *m_d->model.optionData;
    data.write(setting.data());
}

void KisWetPaintOptionWidget::readOptionSetting(const KisPropertiesConfigurationSP setting)
{
    KisWetPaintOptionData data = *m_d->model.optionData;
    data.read(setting.data());
    m_d->model.optionData.set(data);
}
