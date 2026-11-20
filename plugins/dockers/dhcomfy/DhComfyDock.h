/*
 *  SPDX-FileCopyrightText: 2026 Darren Hing
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DH_COMFY_DOCK_H
#define DH_COMFY_DOCK_H

#include <QDockWidget>
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <functional>

#include <KoCanvasObserverBase.h>

#include "DhComfyClient.h"
#include "DhComfyWorkflow.h"

class KisCanvas2;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QWidget;

/**
 * "DH Comfy": run your own ComfyUI workflows (on another machine) on the
 * Krita canvas and get the result back as a layer.
 */
class DhComfyDock : public QDockWidget, public KoCanvasObserverBase
{
    Q_OBJECT
public:
    DhComfyDock();
    ~DhComfyDock() override;

    QString observerName() override { return QStringLiteral("DhComfyDock"); }
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

private Q_SLOTS:
    void slotConnect();
    void slotRefreshWorkflows();
    void slotWorkflowChosen(int index);
    void slotGenerate();
    void slotCancel();
    void slotJobFinished(const QVector<DhComfy::Result> &results);
    void slotJobFailed(const QString &message);

private:
    void buildUi();
    void setStatus(const QString &text, bool error = false);
    void setConnected(bool connected);
    void loadWorkflow(const QString &name);
    void rebuildControls();
    QHash<QString, QJsonValue> currentValues() const;
    void saveValues();
    QHash<QString, QJsonValue> savedValues(const QString &workflow) const;
    void updateButtons();

    QPointer<KisCanvas2> m_canvas;

    DhComfy::Client *m_client;
    DhComfy::Job *m_job;
    DhComfy::NodeInfo m_nodes;
    bool m_connected {false};

    QString m_workflowName;
    QJsonObject m_api;
    QVector<DhComfy::Control> m_controls;
    QHash<QString, std::function<QJsonValue()>> m_getters;

    // where the current run's results go
    QRect m_runRect;
    QImage m_runSelection; ///< Grayscale8, empty when no selection
    QString m_runName;

    QLineEdit *m_server {nullptr};
    QPushButton *m_connectButton {nullptr};
    QLabel *m_serverInfo {nullptr};
    QComboBox *m_workflows {nullptr};
    QPushButton *m_refreshButton {nullptr};
    QScrollArea *m_scroll {nullptr};
    QWidget *m_controlsHost {nullptr};
    QFormLayout *m_form {nullptr};
    QCheckBox *m_useSelection {nullptr};
    QCheckBox *m_fitResult {nullptr};
    QCheckBox *m_maskResult {nullptr};
    QSpinBox *m_padding {nullptr};
    QPushButton *m_generate {nullptr};
    QPushButton *m_cancel {nullptr};
    QLabel *m_status {nullptr};
};

#endif // DH_COMFY_DOCK_H
