#pragma once

#include <QVariantMap>

#include "filepicker.h"

class QDBusPendingCallWatcher;

// The file dialog of xdg-desktop-portal, asked over D-Bus. Taken from
// Omacut (https://github.com/omacom/omacut, MIT, © David Heinemeier
// Hansson), which is the one that survives a portal that stops answering
// or is replaced while its dialog is up.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);
    ~PortalFilePicker() override;

    void openImages() override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    bool connectToRequestPath(const QString &path);
    void clearPending();
    // Best-effort Close of the pending portal request, if there is one.
    void closePendingRequest();

    QString m_pendingPath;
    bool m_pending = false;
    // Bumped per request, so a late reply for a replaced request is ignored.
    int m_pendingGeneration = 0;
    QDBusPendingCallWatcher *m_pendingWatcher = nullptr;
};
