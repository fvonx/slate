#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

// Asks the user for files. The window's own goes through the desktop
// portal; tests put a fake in its place.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openImages() = 0;

signals:
    void imagesSelected(const QList<QUrl> &urls);
    // The dialog could not be shown; the window words the message.
    void failed();
};
