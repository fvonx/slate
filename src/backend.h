#pragma once

#include <QColor>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "board.h"

class FilePicker;

// The boards on disk, one JSON file each, and the one that is open. Every
// change is saved on its own a moment later, so there is nothing to save by
// hand and nothing to lose on quit.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(Board *board READ board CONSTANT)
    Q_PROPERTY(QVariantList boards READ boards NOTIFY boardsChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveErrorChanged)
    Q_PROPERTY(QString imageFolder READ imageFolder CONSTANT)

public:
    // Tests pass a temp directory.
    explicit Backend(const QString &directory = {}, QObject *parent = nullptr);
    ~Backend() override;

    Board *board() { return &m_board; }
    QVariantList boards() const;
    QString currentId() const { return m_current; }
    QString currentName() const;
    // The file a save failed on, or empty; the window words the message.
    QString saveError() const { return m_saveError; }

    // Where the boards' images are kept, as a URL for the window to load
    // them from.
    QString imageFolder() const;

    // Asks for image files; `imagesChosen` has them once they are picked.
    Q_INVOKABLE void pickImages();
    // Copies an image file in among Slate's own, so the board keeps it when
    // the original is moved or deleted, and answers with `ok`, the `name`
    // it has there, and its `width` and `height`; or with the `file` that
    // is no image. The same picture is kept once however often it is added.
    Q_INVOKABLE QVariantMap importImage(const QUrl &url);
    // Tests put a fake in place of the desktop's file dialog.
    void setFilePicker(FilePicker *picker);

    Q_INVOKABLE void newBoard();
    Q_INVOKABLE void open(const QString &id);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE bool save();
    // Saves the open board as "pdf" or "svg" in the Downloads folder, drawn
    // with the colors the window has, and answers with `ok` and the `path`
    // it wrote or could not write.
    Q_INVOKABLE QVariantMap exportBoard(const QString &format, const QVariantMap &colors, const QColor &background,
                                        const QColor &foreground, bool light, int radius);
    // Tests export into a temp directory.
    void setExportDirectory(const QString &directory) { m_exportDirectory = directory; }

signals:
    void boardsChanged();
    void currentChanged();
    void saveErrorChanged();
    void imagesChosen(const QVariantList &urls);
    // The file dialog could not be shown.
    void pickFailed();

private:
    struct Info {
        QString id;
        QString name;
        qint64 created = 0;
    };

    QString path(const QString &id) const;
    int indexOf(const QString &id) const;
    bool write(const QString &id, const QJsonObject &object);
    void rememberCurrent();
    void step(int by);
    QString imagePath() const { return m_directory + "/images"; }
    void forgetUnusedImages(const QSet<QString> &used);

    QString m_directory;
    QString m_exportDirectory;
    QVector<Info> m_boards;
    QString m_current;
    Board m_board;
    bool m_dirty = false;
    QTimer m_saveTimer;
    QString m_saveError;
    FilePicker *m_picker = nullptr;
};
