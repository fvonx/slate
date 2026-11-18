#pragma once

#include <QString>
#include <QStringList>

// The icons that can be placed on a board, in the order the menu shows them.
// Each is an outline drawn on a 24 by 24 square, as an SVG path, so the
// window and the exported files draw it the same way.
const QStringList &iconNames();
QString iconPath(const QString &name);
