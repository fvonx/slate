#include "icons.h"

#include <QHash>

namespace {

struct Icon {
    const char *name;
    const char *path;
};

// Drawn for Slate: simple outlines with round ends, read at any size.
const Icon icons[] = {
    {"star", "M12 2.8 L14.8 8.6 L21.2 9.4 L16.5 13.8 L17.7 20.2 L12 17.1 L6.3 20.2 L7.5 13.8 L2.8 9.4 L9.2 8.6 Z"},
    {"heart", "M12 20 C10 18.6 3 14 3 8.6 A4.6 4.6 0 0 1 12 7 A4.6 4.6 0 0 1 21 8.6 C21 14 14 18.6 12 20 Z"},
    {"check", "M4.5 12.5 L9.5 17.5 L19.5 6.5"},
    {"cross", "M6 6 L18 18 M18 6 L6 18"},
    {"warning", "M12 3.5 L21.5 20 H2.5 Z M12 9.5 V14 M12 17 V17.01"},
    {"info", "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 Z M12 11 V16.5 M12 7.6 V7.61"},
    {"question", "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 Z M9.4 9.6 A2.7 2.7 0 1 1 13.3 12 C12.4 12.5 12 13 12 14 M12 17 V17.01"},
    {"idea", "M9.5 18.5 H14.5 M10.5 21 H13.5 M12 3 A6 6 0 0 0 8.4 13.8 C9.2 14.5 9.5 15.3 9.5 16 V18.5 H14.5 V16 C14.5 15.3 14.8 14.5 15.6 13.8 A6 6 0 0 0 12 3 Z"},
    {"flag", "M5.5 21 V3.5 M5.5 4 H18 L15.5 8 L18 12 H5.5"},
    {"person", "M12 3.5 A4 4 0 1 0 12 11.5 A4 4 0 1 0 12 3.5 Z M4.5 20.5 C5.4 16.6 8.4 14.5 12 14.5 C15.6 14.5 18.6 16.6 19.5 20.5"},
    {"clock", "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 Z M12 7 V12 L15.5 14"},
    {"calendar", "M4 5.5 H20 V20 H4 Z M4 10 H20 M8.5 3 V7.5 M15.5 3 V7.5"},
    {"lock", "M5.5 11 H18.5 V20.5 H5.5 Z M8.5 11 V7.5 A3.5 3.5 0 0 1 15.5 7.5 V11 M12 14.5 V17"},
    {"bolt", "M13.5 2.5 L5 13.5 H11.5 L10.5 21.5 L19 10.5 H12.5 Z"},
    {"target", "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 Z M12 7 A5 5 0 1 0 12 17 A5 5 0 1 0 12 7 Z M12 11 A1 1 0 1 0 12 13 A1 1 0 1 0 12 11 Z"},
    {"home", "M3.5 11 L12 3.5 L20.5 11 M6 9 V20 H18 V9 M10 20 V14.5 H14 V20"},
    {"mail", "M3 6 H21 V18 H3 Z M3 6.5 L12 13 L21 6.5"},
    {"chat", "M4 5 H20 V16 H10.5 L6.5 20 V16 H4 Z"},
    {"search", "M10.5 4 A6.5 6.5 0 1 0 10.5 17 A6.5 6.5 0 1 0 10.5 4 Z M15.3 15.3 L20 20"},
    {"location", "M12 21 C10.5 19.6 5 14.2 5 9.5 A7 7 0 0 1 19 9.5 C19 14.2 13.5 19.6 12 21 Z M12 7 A2.5 2.5 0 1 0 12 12 A2.5 2.5 0 1 0 12 7 Z"},
    {"arrow", "M4 12 H20 M14 6 L20 12 L14 18"},
};

} // namespace

const QStringList &iconNames() {
    static const QStringList names = [] {
        QStringList list;
        for (const Icon &icon : icons)
            list << QString::fromLatin1(icon.name);
        return list;
    }();
    return names;
}

QString iconPath(const QString &name) {
    static const QHash<QString, QString> paths = [] {
        QHash<QString, QString> hash;
        for (const Icon &icon : icons)
            hash.insert(QString::fromLatin1(icon.name), QString::fromLatin1(icon.path));
        return hash;
    }();
    return paths.value(name);
}
