#pragma once

#include <QColor>
#include <QHash>
#include <QRectF>
#include <QString>

class Board;
class QPainter;

// What the window draws a board with: the theme's colors, whether it is a
// light one, and how round its corners are.
struct BoardLook {
    QColor background;
    QColor foreground;
    QHash<QString, QColor> colors;
    bool light = false;
    int radius = 0;
    // Where the files of the board's images are.
    QString imageFolder;
};

// The part of the canvas an exported file shows: everything on the board,
// with a margin around it.
QRectF exportArea(const Board &board);

// Draws the board in canvas coordinates, as the window shows it.
void paintBoard(QPainter &painter, const Board &board, const BoardLook &look);

// Writes the board to `path` as "pdf" or "svg". The file appears whole or
// not at all.
bool exportBoard(const Board &board, const BoardLook &look, const QString &format, const QString &title,
                 const QString &path);
