#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace {

struct PortalFilterRule {
    uint type;
    QString pattern;
};

using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};

using PortalFileFilters = QList<PortalFileFilter>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

void registerPortalFilterTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        return true;
    }();
    Q_UNUSED(registered);
}

// A rule is a glob (0) or a MIME type (1).
PortalFileFilter imageFilter() {
    return {
        PortalFilePicker::tr("Images"),
        {
            {1, QStringLiteral("image/png")},
            {1, QStringLiteral("image/jpeg")},
            {1, QStringLiteral("image/webp")},
            {1, QStringLiteral("image/gif")},
            {1, QStringLiteral("image/bmp")},
            {1, QStringLiteral("image/svg+xml")},
            {0, QStringLiteral("*.png")},
            {0, QStringLiteral("*.jpg")},
            {0, QStringLiteral("*.jpeg")},
            {0, QStringLiteral("*.webp")},
            {0, QStringLiteral("*.gif")},
            {0, QStringLiteral("*.bmp")},
            {0, QStringLiteral("*.svg")},
        },
    };
}

QString portalToken() {
    return QStringLiteral("slate_%1").arg(QRandomGenerator::global()->generate());
}

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}

QString openFolder() {
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir(pictures).exists() ? pictures : QDir::homePath();
}

} // namespace

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {
    registerPortalFilterTypes();
}

PortalFilePicker::~PortalFilePicker() {
    closePendingRequest();
}

bool PortalFilePicker::connectToRequestPath(const QString &path) {
    m_pendingPath = path;
    return QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(handleResponse(uint,QVariantMap)));
}

void PortalFilePicker::closePendingRequest() {
    if (m_pendingPath.isEmpty())
        return;
    // Sent directly rather than through QDBusInterface, whose constructor
    // blocks on a synchronous Introspect and could stall shutdown if the
    // portal hung.
    QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close")));
}

void PortalFilePicker::openImages() {
    // A request whose Response never arrives (the portal crashed or was
    // killed with the dialog up) must not wedge every later dialog behind
    // it: close the stuck one best-effort and replace it.
    if (m_pending) {
        closePendingRequest();
        clearPending();
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"), bus);
    if (!portal.isValid()) {
        emit failed();
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("accept_label"), tr("Add"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(openFolder()));
    options.insert(QStringLiteral("filters"),
                   QVariant::fromValue(PortalFileFilters{
                       imageFilter(), {tr("All files"), {{0, QStringLiteral("*")}}}}));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(imageFilter()));

    // Subscribe to the Response signal at the request path the portal will
    // derive from our handle_token *before* making the call, so a response
    // can't slip past while our match rule is still being installed.
    const QString token = portalToken();
    options.insert(QStringLiteral("handle_token"), token);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    m_pending = true;
    if (!connectToRequestPath(predictedPath)) {
        clearPending();
        emit failed();
        return;
    }

    const int generation = ++m_pendingGeneration;
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(QStringLiteral("OpenFile"), QString(), tr("Add images"), options), this);
    m_pendingWatcher = watcher;

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_pendingWatcher == watcher)
            m_pendingWatcher = nullptr;

        // Superseded by a newer request, or a response may already have
        // been handled while the reply was in flight.
        if (generation != m_pendingGeneration || !m_pending)
            return;

        if (reply.isError()) {
            clearPending();
            emit failed();
            return;
        }

        // Old portal versions can hand back a different request path than the
        // handle_token predicts; move our subscription over if so.
        const QString actualPath = reply.value().path();
        if (actualPath != m_pendingPath) {
            QDBusConnection::sessionBus().disconnect(
                QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                this, SLOT(handleResponse(uint,QVariantMap)));
            if (!connectToRequestPath(actualPath)) {
                clearPending();
                emit failed();
            }
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    clearPending();
    // Anything but 0 is a dialog that was cancelled or closed.
    if (response != 0)
        return;
    QList<QUrl> urls;
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    for (const QString &uri : uris)
        urls.append(QUrl(uri));
    if (!urls.isEmpty())
        emit imagesSelected(urls);
}

void PortalFilePicker::clearPending() {
    // A caller that never gets its reply must not outlive the request it
    // belonged to (repeated stuck requests would otherwise pile them up).
    if (m_pendingWatcher) {
        m_pendingWatcher->deleteLater();
        m_pendingWatcher = nullptr;
    }
    if (!m_pendingPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
    }
    m_pendingPath.clear();
    m_pending = false;
}
