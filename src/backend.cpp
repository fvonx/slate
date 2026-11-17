#include "backend.h"

#include "boardexport.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

// Null when the file is missing or isn't a board, so it is never mistaken
// for an empty one and written over.
bool readBoard(const QString &path, QJsonObject *object) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *object = document.object();
    return true;
}

} // namespace

Backend::Backend(const QString &directory, QObject *parent)
    : QObject(parent),
      m_directory(directory.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/boards"
                      : directory) {
    QDir().mkpath(m_directory);

    const auto files = QDir(m_directory).entryInfoList({"*.json"}, QDir::Files);
    for (const QFileInfo &file : files) {
        QJsonObject object;
        if (!readBoard(file.filePath(), &object))
            continue;
        m_boards.append({file.completeBaseName(), object.value("name").toString(),
                         qint64(object.value("created").toDouble())});
    }
    std::sort(m_boards.begin(), m_boards.end(), [](const Info &a, const Info &b) {
        return a.created != b.created ? a.created < b.created : a.id < b.id;
    });

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &Backend::save);
    connect(&m_board, &Board::changed, this, [this] {
        m_dirty = true;
        m_saveTimer.start();
    });

    QFile last(m_directory + "/.current");
    const QString remembered = last.open(QIODevice::ReadOnly) ? QString::fromUtf8(last.readAll()).trimmed()
                                                              : QString();
    if (m_boards.isEmpty())
        newBoard();
    else
        open(indexOf(remembered) >= 0 ? remembered : m_boards.last().id);
}

Backend::~Backend() {
    save();
}

QString Backend::path(const QString &id) const {
    return m_directory + "/" + id + ".json";
}

int Backend::indexOf(const QString &id) const {
    for (int i = 0; i < m_boards.size(); ++i)
        if (m_boards[i].id == id)
            return i;
    return -1;
}

QVariantList Backend::boards() const {
    QVariantList list;
    for (const Info &info : m_boards)
        list.append(QVariantMap{{"id", info.id}, {"name", info.name}});
    return list;
}

QString Backend::currentName() const {
    const int i = indexOf(m_current);
    return i < 0 ? QString() : m_boards[i].name;
}

// Atomic: the old file stays whole unless the new one is completely written.
bool Backend::write(const QString &id, const QJsonObject &object) {
    QSaveFile file(path(id));
    file.setDirectWriteFallback(false);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    const QString error = ok ? QString() : path(id);
    if (error != m_saveError) {
        m_saveError = error;
        emit saveErrorChanged();
    }
    return ok;
}

bool Backend::save() {
    m_saveTimer.stop();
    const int i = indexOf(m_current);
    if (!m_dirty || i < 0)
        return true;
    QJsonObject object = m_board.toJson();
    object["name"] = m_boards[i].name;
    object["created"] = double(m_boards[i].created);
    if (!write(m_current, object))
        return false;
    m_dirty = false;
    return true;
}

void Backend::rememberCurrent() {
    QSaveFile file(m_directory + "/.current");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_current.toUtf8());
        file.commit();
    }
}

void Backend::newBoard() {
    save();
    Info info;
    info.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    info.name = tr("Board %1").arg(m_boards.size() + 1);
    // Boards are listed in the order they were made.
    info.created = std::max(QDateTime::currentMSecsSinceEpoch(),
                            m_boards.isEmpty() ? 0 : m_boards.last().created + 1);
    m_boards.append(info);
    m_current = info.id;
    m_board.load({});
    m_dirty = true;
    save();
    rememberCurrent();
    emit boardsChanged();
    emit currentChanged();
}

void Backend::open(const QString &id) {
    if (id == m_current || indexOf(id) < 0)
        return;
    save();
    QJsonObject object;
    if (!readBoard(path(id), &object))
        return;
    m_current = id;
    m_board.load(object);
    m_dirty = false;
    rememberCurrent();
    emit currentChanged();
}

void Backend::rename(const QString &id, const QString &name) {
    const int i = indexOf(id);
    const QString trimmed = name.trimmed();
    if (i < 0 || trimmed.isEmpty() || trimmed == m_boards[i].name)
        return;
    m_boards[i].name = trimmed;
    if (id == m_current) {
        m_dirty = true;
        save();
        emit currentChanged();
    } else {
        QJsonObject object;
        if (readBoard(path(id), &object)) {
            object["name"] = trimmed;
            write(id, object);
        }
    }
    emit boardsChanged();
}

void Backend::remove(const QString &id) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    QFile::remove(path(id));
    m_boards.removeAt(i);
    if (id == m_current) {
        // Nothing of the deleted board is written back.
        m_saveTimer.stop();
        m_dirty = false;
        m_current.clear();
        if (m_boards.isEmpty()) {
            newBoard();
            return;
        }
        open(m_boards[std::min(i, int(m_boards.size()) - 1)].id);
    }
    emit boardsChanged();
}

QVariantMap Backend::exportBoard(const QString &format, const QVariantMap &colors, const QColor &background,
                                 const QColor &foreground, bool light, int radius) {
    const QString folder = !m_exportDirectory.isEmpty()
                               ? m_exportDirectory
                               : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    // Named after the board, without what a file name can't hold, and never
    // over a file that is already there.
    static const QRegularExpression unsafe("[/\\\\\\x00-\\x1f]");
    QString name = currentName();
    name.replace(unsafe, "-");
    name = name.trimmed();
    if (name.isEmpty() || name.startsWith('.'))
        name.prepend("Board");
    QString path = folder + "/" + name + "." + format;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = folder + "/" + name + " " + QString::number(n) + "." + format;

    BoardLook look;
    look.background = background;
    look.foreground = foreground;
    look.light = light;
    look.radius = radius;
    for (auto it = colors.begin(); it != colors.end(); ++it)
        look.colors.insert(it.key(), it.value().value<QColor>());

    const bool known = format == QLatin1String("pdf") || format == QLatin1String("svg");
    const bool ok = known && m_board.count() > 0 && QDir().mkpath(folder)
                 && ::exportBoard(m_board, look, format, currentName(), path);
    return {{"ok", ok}, {"path", path}};
}

void Backend::step(int by) {
    const int i = indexOf(m_current);
    if (i < 0 || m_boards.size() < 2)
        return;
    open(m_boards[(i + by + m_boards.size()) % m_boards.size()].id);
}

void Backend::next() {
    step(1);
}

void Backend::previous() {
    step(-1);
}
