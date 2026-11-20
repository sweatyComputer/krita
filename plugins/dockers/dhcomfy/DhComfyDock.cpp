/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DhComfyDock.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <kis_canvas2.h>
#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_node_commands_adapter.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>
#include <kis_selection.h>
#include <kis_slider_spin_box.h>

using namespace DhComfy;

namespace {

KConfigGroup config()
{
    return KSharedConfig::openConfig()->group("DhComfy");
}

QByteArray encodePng(const QImage &img)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return data;
}

QLabel *header(const QString &text, QWidget *parent)
{
    QLabel *l = new QLabel(QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()), parent);
    l->setContentsMargins(0, 8, 0, 0);
    return l;
}

} // namespace

DhComfyDock::DhComfyDock()
    : QDockWidget(i18n("DH Comfy"))
    , m_client(new Client(this))
    , m_job(new Job(m_client, this))
{
    buildUi();
    connect(m_job, &Job::status, this, [this](const QString &t) { setStatus(t); });
    connect(m_job, &Job::finished, this, &DhComfyDock::slotJobFinished);
    connect(m_job, &Job::failed, this, &DhComfyDock::slotJobFailed);

    const QString server = config().readEntry("server", QString());
    m_server->setText(server);
    setConnected(false);
    if (!server.isEmpty()) QMetaObject::invokeMethod(this, "slotConnect", Qt::QueuedConnection);
}

DhComfyDock::~DhComfyDock()
{
    saveValues();
}

void DhComfyDock::buildUi()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setContentsMargins(6, 6, 6, 6);

    // server
    QHBoxLayout *srv = new QHBoxLayout();
    m_server = new QLineEdit(page);
    m_server->setPlaceholderText(i18n("PC address, e.g. 192.168.1.20:8188"));
    m_server->setInputMethodHints(Qt::ImhNoAutoUppercase | Qt::ImhUrlCharactersOnly);
    m_connectButton = new QPushButton(i18n("Connect"), page);
    srv->addWidget(m_server, 1);
    srv->addWidget(m_connectButton);
    v->addLayout(srv);
    m_serverInfo = new QLabel(page);
    m_serverInfo->setWordWrap(true);
    v->addWidget(m_serverInfo);

    // workflow
    QHBoxLayout *wf = new QHBoxLayout();
    m_workflows = new QComboBox(page);
    m_workflows->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_workflows->setMinimumContentsLength(10);
    m_refreshButton = new QPushButton(i18n("Refresh"), page);
    wf->addWidget(m_workflows, 1);
    wf->addWidget(m_refreshButton);
    v->addLayout(wf);

    // knobs
    m_scroll = new QScrollArea(page);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_controlsHost = new QWidget(m_scroll);
    m_form = new QFormLayout(m_controlsHost);
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_scroll->setWidget(m_controlsHost);
    v->addWidget(m_scroll, 1);

    // canvas options
    m_useSelection = new QCheckBox(i18n("Use selection"), page);
    m_useSelection->setToolTip(i18n("Send only the selected area (plus padding) and put the result back there."));
    m_fitResult = new QCheckBox(i18n("Fit result to area"), page);
    m_fitResult->setToolTip(i18n("Scale the result to the area that was sent. Turn off for upscalers."));
    m_maskResult = new QCheckBox(i18n("Mask result to selection"), page);
    m_padding = new QSpinBox(page);
    m_padding->setRange(0, 1024);
    m_padding->setSuffix(i18n(" px"));
    m_padding->setToolTip(i18n("Extra context around the selection"));

    const KConfigGroup cfg = config();
    m_useSelection->setChecked(cfg.readEntry("useSelection", true));
    m_fitResult->setChecked(cfg.readEntry("fitResult", true));
    m_maskResult->setChecked(cfg.readEntry("maskResult", true));
    m_padding->setValue(cfg.readEntry("padding", 32));

    QHBoxLayout *opts1 = new QHBoxLayout();
    opts1->addWidget(m_useSelection);
    opts1->addWidget(new QLabel(i18n("Padding"), page));
    opts1->addWidget(m_padding);
    opts1->addStretch(1);
    v->addLayout(opts1);
    QHBoxLayout *opts2 = new QHBoxLayout();
    opts2->addWidget(m_fitResult);
    opts2->addWidget(m_maskResult);
    opts2->addStretch(1);
    v->addLayout(opts2);

    // run
    QHBoxLayout *run = new QHBoxLayout();
    m_generate = new QPushButton(i18n("Generate"), page);
    m_generate->setMinimumHeight(40);
    m_cancel = new QPushButton(i18n("Cancel"), page);
    m_cancel->setMinimumHeight(40);
    run->addWidget(m_generate, 2);
    run->addWidget(m_cancel, 1);
    v->addLayout(run);

    m_status = new QLabel(page);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_status);

    setWidget(page);

    connect(m_connectButton, &QPushButton::clicked, this, &DhComfyDock::slotConnect);
    connect(m_server, &QLineEdit::returnPressed, this, &DhComfyDock::slotConnect);
    connect(m_refreshButton, &QPushButton::clicked, this, &DhComfyDock::slotRefreshWorkflows);
    connect(m_workflows, QOverload<int>::of(&QComboBox::activated), this, &DhComfyDock::slotWorkflowChosen);
    connect(m_generate, &QPushButton::clicked, this, &DhComfyDock::slotGenerate);
    connect(m_cancel, &QPushButton::clicked, this, &DhComfyDock::slotCancel);
    for (QCheckBox *c : {m_useSelection, m_fitResult, m_maskResult}) {
        connect(c, &QCheckBox::toggled, this, [this]() {
            KConfigGroup cfg = config();
            cfg.writeEntry("useSelection", m_useSelection->isChecked());
            cfg.writeEntry("fitResult", m_fitResult->isChecked());
            cfg.writeEntry("maskResult", m_maskResult->isChecked());
        });
    }
    connect(m_padding, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) { config().writeEntry("padding", v); });
}

void DhComfyDock::setCanvas(KoCanvasBase *canvas)
{
    m_canvas = dynamic_cast<KisCanvas2 *>(canvas);
    updateButtons();
}

void DhComfyDock::unsetCanvas()
{
    m_canvas = nullptr;
    updateButtons();
}

void DhComfyDock::setStatus(const QString &text, bool error)
{
    m_status->setText(error ? QStringLiteral("<span style='color:#e66'>%1</span>").arg(text.toHtmlEscaped().replace('\n', "<br>"))
                            : text.toHtmlEscaped());
}

void DhComfyDock::setConnected(bool connected)
{
    m_connected = connected;
    m_workflows->setEnabled(connected);
    m_refreshButton->setEnabled(connected);
    updateButtons();
}

void DhComfyDock::updateButtons()
{
    const bool running = m_job->isRunning();
    m_generate->setEnabled(m_connected && !running && m_canvas && !m_api.isEmpty());
    m_cancel->setEnabled(running);
    m_connectButton->setEnabled(!running);
}

// ---------------------------------------------------------------------------
// connection, workflow list

void DhComfyDock::slotConnect()
{
    if (m_server->text().trimmed().isEmpty()) {
        setStatus(i18n("Enter your PC's address first (where ComfyUI runs with --listen)."), true);
        return;
    }
    m_client->setServer(m_server->text());
    config().writeEntry("server", m_server->text().trimmed());
    setConnected(false);
    setStatus(i18n("Connecting to %1…", m_client->server().toString()));

    m_client->get(QStringLiteral("/system_stats"), QUrlQuery(), [this](const Reply &r) {
        if (!r.ok()) {
            m_serverInfo->clear();
            setStatus(i18n("Can't reach ComfyUI at %1 (%2).\nIs it running with --listen, and allowed through the Windows firewall?",
                           m_client->server().toString(), r.error), true);
            return;
        }
        const QJsonObject o = r.json().object();
        const QJsonObject sys = o.value("system").toObject();
        const QJsonArray devs = o.value("devices").toArray();
        QString gpu;
        if (!devs.isEmpty()) {
            const QJsonObject d = devs.first().toObject();
            gpu = QStringLiteral("%1, %2 GB free").arg(d.value("name").toString().section(':', 1).trimmed())
                      .arg(d.value("vram_free").toDouble() / 1073741824.0, 0, 'f', 1);
        }
        m_serverInfo->setText(i18n("ComfyUI %1 · %2", sys.value("comfyui_version").toString(), gpu));
        setStatus(i18n("Reading the server's nodes and models…"));

        m_client->get(QStringLiteral("/object_info"), QUrlQuery(), [this](const Reply &r2) {
            if (!r2.ok()) {
                setStatus(i18n("Connected, but couldn't read the node list (%1).", r2.error), true);
                return;
            }
            m_nodes = NodeInfo::fromJson(r2.body);
            setConnected(true);
            setStatus(i18n("Connected: %1 node types.", m_nodes.size()));
            slotRefreshWorkflows();
        }, 120000);
    }, 8000);
}

void DhComfyDock::slotRefreshWorkflows()
{
    QUrlQuery q;
    q.addQueryItem("dir", "workflows");
    q.addQueryItem("recurse", "true");
    q.addQueryItem("split", "false");
    m_client->get(QStringLiteral("/userdata"), q, [this](const Reply &r) {
        if (!r.ok()) {
            setStatus(i18n("Couldn't list workflows (%1).", r.error), true);
            return;
        }
        QStringList names;
        for (const QJsonValue &v : r.json().array()) {
            const QString n = v.toString();
            if (n.endsWith(".json", Qt::CaseInsensitive)) names << n;
        }
        names.sort(Qt::CaseInsensitive);

        const QString want = m_workflowName.isEmpty() ? config().readEntry("workflow", QString()) : m_workflowName;
        m_workflows->clear();
        m_workflows->addItem(i18n("Choose a workflow…"), QString());
        for (const QString &n : names) {
            QString label = n;
            label.chop(5);
            m_workflows->addItem(label, n);
        }
        const int idx = m_workflows->findData(want);
        if (idx > 0) {
            m_workflows->setCurrentIndex(idx);
            loadWorkflow(want);
        } else if (names.isEmpty()) {
            setStatus(i18n("No saved workflows on the server yet. Save one in ComfyUI (Workflow > Save)."));
        }
    });
}

void DhComfyDock::slotWorkflowChosen(int index)
{
    const QString name = m_workflows->itemData(index).toString();
    if (name.isEmpty() || name == m_workflowName) return;
    loadWorkflow(name);
}

void DhComfyDock::loadWorkflow(const QString &name)
{
    saveValues();
    setStatus(i18n("Loading %1…", name));
    m_client->getUserData(QStringLiteral("workflows/") + name, [this, name](const Reply &r) {
        if (!r.ok()) {
            setStatus(i18n("Couldn't load %1 (%2).", name, r.error), true);
            return;
        }
        QString error;
        const QJsonObject api = toApi(r.json().object(), m_nodes, &error);
        if (api.isEmpty()) {
            m_api = QJsonObject();
            m_controls.clear();
            rebuildControls();
            setStatus(error, true);
            return;
        }
        m_workflowName = name;
        config().writeEntry("workflow", name);
        m_api = api;
        m_controls = analyse(api, m_nodes);
        rebuildControls();
        setStatus(i18n("Ready: %1 nodes.", api.size()));
        updateButtons();
    });
}

// ---------------------------------------------------------------------------
// knobs

void DhComfyDock::rebuildControls()
{
    while (m_form->rowCount() > 0) m_form->removeRow(0);
    m_getters.clear();

    const QHash<QString, QJsonValue> saved = savedValues(m_workflowName);
    QString group;
    for (const Control &c : m_controls) {
        const QJsonValue v = saved.contains(c.key) ? saved.value(c.key) : c.value;
        if (c.group != group) {
            group = c.group;
            m_form->addRow(header(group, m_controlsHost));
        }

        switch (c.kind) {
        case Control::Prompt:
        case Control::NegativePrompt: {
            QPlainTextEdit *e = new QPlainTextEdit(m_controlsHost);
            e->setPlainText(v.toString());
            e->setTabChangesFocus(true);
            const int line = e->fontMetrics().lineSpacing();
            e->setFixedHeight(line * (c.kind == Control::Prompt ? 5 : 3) + 12);
            m_form->addRow(c.label, e);
            m_getters.insert(c.key, [e]() { return QJsonValue(e->toPlainText()); });
            break;
        }
        case Control::Text: {
            QLineEdit *e = new QLineEdit(v.toString(), m_controlsHost);
            m_form->addRow(c.label, e);
            m_getters.insert(c.key, [e]() { return QJsonValue(e->text()); });
            break;
        }
        case Control::Int: {
            KisSliderSpinBox *s = new KisSliderSpinBox(m_controlsHost);
            const int mn = c.hasRange ? int(qMax(c.min, -2147483647.0)) : 0;
            const int mx = c.hasRange ? int(qMin(c.max, 2147483647.0)) : 100000;
            s->setRange(mn, mx);
            if (c.hasRange && mx - mn > 200) s->setSoftRange(mn, qMin(mx, mn + 200));
            s->setValue(int(v.toVariant().toLongLong()));
            m_form->addRow(c.label, s);
            m_getters.insert(c.key, [s]() { return QJsonValue(s->value()); });
            break;
        }
        case Control::Float: {
            KisDoubleSliderSpinBox *s = new KisDoubleSliderSpinBox(m_controlsHost);
            const double mn = c.hasRange ? c.min : 0.0, mx = c.hasRange ? c.max : 100.0;
            const int decimals = (c.step > 0 && c.step < 0.05) ? 3 : 2;
            s->setRange(mn, qMin(mx, 1e6), decimals);
            if (mx - mn > 30) s->setSoftRange(mn, qMin(mx, mn + 30));
            s->setValue(v.toDouble());
            m_form->addRow(c.label, s);
            m_getters.insert(c.key, [s]() { return QJsonValue(s->value()); });
            break;
        }
        case Control::Bool:
        case Control::CanvasSize: {
            QCheckBox *b = new QCheckBox(c.kind == Control::CanvasSize ? c.label : QString(), m_controlsHost);
            b->setChecked(v.toBool());
            if (c.kind == Control::CanvasSize) m_form->addRow(b);
            else m_form->addRow(c.label, b);
            m_getters.insert(c.key, [b]() { return QJsonValue(b->isChecked()); });
            break;
        }
        case Control::Choice: {
            QComboBox *cb = new QComboBox(m_controlsHost);
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            cb->setMinimumContentsLength(8);
            cb->addItems(c.options);
            int idx = cb->findText(v.toString());
            if (idx < 0) {
                // model gone from the server: show it, so it's obvious why it fails
                cb->insertItem(0, v.toString() + i18n(" (missing)"), v.toString());
                idx = 0;
            }
            cb->setCurrentIndex(idx);
            m_form->addRow(c.label, cb);
            m_getters.insert(c.key, [cb]() {
                const QVariant d = cb->currentData();
                return QJsonValue(d.isValid() ? d.toString() : cb->currentText());
            });
            break;
        }
        case Control::Seed: {
            QWidget *row = new QWidget(m_controlsHost);
            QHBoxLayout *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            QLineEdit *e = new QLineEdit(row);
            e->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{1,15}"), e));
            QCheckBox *rnd = new QCheckBox(i18n("Random"), row);
            const QJsonObject so = v.toObject();
            e->setText(QString::number(so.value("value").toVariant().toLongLong()));
            rnd->setChecked(so.value("random").toBool(true));
            e->setEnabled(!rnd->isChecked());
            connect(rnd, &QCheckBox::toggled, e, [e](bool on) { e->setEnabled(!on); });
            h->addWidget(e, 1);
            h->addWidget(rnd);
            m_form->addRow(c.label, row);
            m_getters.insert(c.key, [e, rnd]() {
                return QJsonValue(QJsonObject {{"value", e->text().toLongLong()}, {"random", rnd->isChecked()}});
            });
            break;
        }
        case Control::ImageInput:
        case Control::MaskInput: {
            QComboBox *cb = new QComboBox(m_controlsHost);
            cb->addItems(c.options);
            cb->setCurrentIndex(qBound(0, v.toInt(), c.options.size() - 1));
            m_form->addRow(c.label, cb);
            m_getters.insert(c.key, [cb]() { return QJsonValue(cb->currentIndex()); });
            break;
        }
        }
    }
    if (m_controls.isEmpty() && !m_api.isEmpty()) {
        m_form->addRow(new QLabel(i18n("No settings to show: the workflow runs as saved."), m_controlsHost));
    }
}

QHash<QString, QJsonValue> DhComfyDock::currentValues() const
{
    QHash<QString, QJsonValue> out;
    for (auto it = m_getters.begin(); it != m_getters.end(); ++it) out.insert(it.key(), it.value()());
    return out;
}

void DhComfyDock::saveValues()
{
    if (m_workflowName.isEmpty() || m_getters.isEmpty()) return;
    QJsonObject o;
    const auto values = currentValues();
    for (auto it = values.begin(); it != values.end(); ++it) o.insert(it.key(), it.value());
    config().writeEntry(QStringLiteral("values:%1").arg(m_workflowName),
                        QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

QHash<QString, QJsonValue> DhComfyDock::savedValues(const QString &workflow) const
{
    QHash<QString, QJsonValue> out;
    if (workflow.isEmpty()) return out;
    const QString s = config().readEntry(QStringLiteral("values:%1").arg(workflow), QString());
    const QJsonObject o = QJsonDocument::fromJson(s.toUtf8()).object();
    for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), it.value());
    return out;
}

// ---------------------------------------------------------------------------
// run

void DhComfyDock::slotGenerate()
{
    if (!m_canvas || m_api.isEmpty() || m_job->isRunning()) return;
    KisViewManager *vm = m_canvas->viewManager();
    KisImageSP image = m_canvas->image();
    if (!vm || !image) return;

    saveValues();
    const QHash<QString, QJsonValue> values = currentValues();
    const bool wantCanvas = needsCanvas(m_api, m_controls, values);
    const bool wantMask = needsSelectionMask(m_api, m_controls, values);

    // area to send
    KisSelectionSP selection = vm->selection();
    const bool useSel = selection && m_useSelection->isChecked() && !selection->selectedExactRect().isEmpty();
    QRect rect = image->bounds();
    if (useSel) {
        const int pad = m_padding->value();
        rect = selection->selectedExactRect().adjusted(-pad, -pad, pad, pad).intersected(image->bounds());
    }
    if (rect.isEmpty()) {
        setStatus(i18n("Nothing to send: the area is empty."), true);
        return;
    }

    QImage canvasImg, selImg;
    image->barrierLock(true);
    if (wantCanvas) canvasImg = image->projection()->convertToQImage(nullptr, rect);
    if (useSel) {
        selImg = QImage(rect.size(), QImage::Format_Grayscale8);
        QVector<quint8> buf(rect.width() * rect.height());
        selection->projection()->readBytes(buf.data(), rect);
        for (int y = 0; y < rect.height(); y++) {
            memcpy(selImg.scanLine(y), buf.constData() + y * rect.width(), size_t(rect.width()));
        }
    }
    image->unlock();

    QVector<Job::Upload> uploads;
    KritaInputs k;
    k.rect = rect;
    k.hasSelection = useSel;
    k.seed = qint64(QRandomGenerator::global()->generate64() & 0x3FFFFFFFFFFFULL);
    if (wantCanvas) {
        QImage alpha(canvasImg.size(), QImage::Format_Grayscale8);
        const QImage argb = canvasImg.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < argb.height(); y++) {
            const QRgb *src = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
            uchar *dst = alpha.scanLine(y);
            for (int x = 0; x < argb.width(); x++) dst[x] = uchar(qAlpha(src[x]));
        }
        k.canvasFile = QStringLiteral("dhcomfy_canvas.png");
        k.canvasMaskFile = QStringLiteral("dhcomfy_canvas_alpha.png");
        uploads.append({k.canvasFile, encodePng(argb)});
        uploads.append({k.canvasMaskFile, encodePng(alpha)});
    }
    if (wantMask) {
        QImage mask = selImg;
        if (mask.isNull()) {
            mask = QImage(rect.size(), QImage::Format_Grayscale8);
            mask.fill(255);
        }
        k.selectionMaskFile = QStringLiteral("dhcomfy_selection.png");
        uploads.append({k.selectionMaskFile, encodePng(mask)});
    }

    const Prepared p = prepare(m_api, m_controls, values, k);
    if (!p.error.isEmpty()) {
        setStatus(p.error, true);
        return;
    }

    m_runRect = rect;
    m_runSelection = (useSel && m_maskResult->isChecked()) ? selImg : QImage();
    m_runName = m_workflows->currentText();
    m_job->start(uploads, p.prompt, p.outputs);
    updateButtons();
}

void DhComfyDock::slotCancel()
{
    m_job->cancel();
    updateButtons();
}

void DhComfyDock::slotJobFailed(const QString &message)
{
    setStatus(message, message != i18n("Cancelled.") && message != QStringLiteral("Cancelled."));
    updateButtons();
}

void DhComfyDock::slotJobFinished(const QVector<Result> &results)
{
    updateButtons();
    if (!m_canvas) {
        setStatus(i18n("Finished, but the document was closed."), true);
        return;
    }
    KisViewManager *vm = m_canvas->viewManager();
    KisImageSP image = m_canvas->image();
    if (!vm || !image) return;

    int n = 0;
    for (const Result &r : results) {
        QImage img = r.image.convertToFormat(QImage::Format_ARGB32);
        if (m_fitResult->isChecked() && img.size() != m_runRect.size()) {
            img = img.scaled(m_runRect.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (!m_runSelection.isNull() && img.size() == m_runSelection.size()) {
            for (int y = 0; y < img.height(); y++) {
                QRgb *px = reinterpret_cast<QRgb *>(img.scanLine(y));
                const uchar *m = m_runSelection.constScanLine(y);
                for (int x = 0; x < img.width(); x++) {
                    const int a = qAlpha(px[x]) * m[x] / 255;
                    px[x] = qRgba(qRed(px[x]), qGreen(px[x]), qBlue(px[x]), a);
                }
            }
        }

        const QString name = r.output.name.isEmpty()
            ? QStringLiteral("%1 %2").arg(m_runName, QTime::currentTime().toString("HH:mm:ss"))
            : r.output.name;
        KisPaintLayerSP layer = new KisPaintLayer(image.data(), name, OPACITY_OPAQUE_U8, image->colorSpace());
        layer->paintDevice()->convertFromQImage(img, nullptr, m_runRect.x() + r.output.offsetX,
                                                m_runRect.y() + r.output.offsetY);

        KisNodeSP above = vm->activeNode();
        while (above && above->inherits("KisMask")) above = above->parent();
        KisNodeSP parent = above ? above->parent() : KisNodeSP(image->root());
        if (!parent) parent = image->root();
        KisNodeCommandsAdapter adapter(vm);
        adapter.addNode(layer, parent, above);
        n++;
    }
    setStatus(i18np("Done: added 1 layer.", "Done: added %1 layers.", n));
}
