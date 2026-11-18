#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <QSignalSpy>
#include <QWheelEvent>
#include <functional>
#include <cmath>

#include "backend.h"
#include "dotgrid.h"
#include "theme.h"
#include "translator.h"

namespace {

bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

int lines(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
}

// A hyprctl that answers with response.json beside it and counts its calls.
QString fakeHyprctl(const QString &directory) {
    const QString command = directory + "/hyprctl";
    write(command, "#!/bin/bash\necho >> '" + directory.toUtf8() + "/calls'\ncat '" + directory.toUtf8()
                       + "/response.json'\n");
    QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return command;
}

QJsonObject readCatalog(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// The placeholders in a text, sorted: %1 to %99, and the count (%n, %Ln).
QStringList placeholders(const QString &text) {
    static const QRegularExpression placeholder("%L?(?:[1-9][0-9]?|n)");
    QStringList found;
    for (auto it = placeholder.globalMatch(text); it.hasNext();)
        found << it.next().captured();
    found.sort();
    return found;
}

// Every text the sources hand to qsTr() or tr(), and whether a call passes it
// a count. It walks the code a token at a time, so a string never hides a
// comment or the other way round, C++'s adjacent literals join, and QML's
// single quotes count. A text that isn't a literal can't be in a catalog.
QMap<QString, bool> translatables(const QString &folder, QStringList *problems) {
    QMap<QString, bool> found;
    for (const QFileInfo &info : QDir(folder).entryInfoList({"*.qml", "*.js", "*.cpp", "*.h"}, QDir::Files)) {
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString code = QString::fromUtf8(file.readAll());
        qsizetype i = 0;
        auto literal = [&](QString *text) {
            const QChar quote = code[i++];
            for (; i < code.size() && code[i] != quote; ++i) {
                if (code[i] == '\\' && i + 1 < code.size()) {
                    const QChar escaped = code[++i];
                    const int digits = escaped == 'u' ? 4 : escaped == 'x' ? 2 : 0;
                    bool hex = false;
                    const ushort unit = code.mid(i + 1, digits).toUShort(&hex, 16);
                    if (digits && hex) {
                        *text += QChar(unit);
                        i += digits;
                    } else {
                        *text += escaped == 'n' ? QChar('\n') : escaped == 't' ? QChar('\t') : escaped;
                    }
                } else {
                    *text += code[i];
                }
            }
            ++i;
        };
        auto skipSpace = [&] {
            while (i < code.size() && code[i].isSpace())
                ++i;
        };
        while (i < code.size()) {
            const QChar c = code[i];
            if (code.mid(i, 2) == "//") {
                i = code.indexOf('\n', i);
                if (i < 0)
                    break;
            } else if (code.mid(i, 2) == "/*") {
                i = code.indexOf("*/", i + 2);
                if (i < 0)
                    break;
                i += 2;
            } else if (c == '"' || c == '\'') {
                QString skipped;
                literal(&skipped);
            } else if (c.isLetter() || c == '_') {
                const qsizetype start = i;
                while (i < code.size() && (code[i].isLetterOrNumber() || code[i] == '_'))
                    ++i;
                const QString word = code.mid(start, i - start);
                skipSpace();
                if ((word != "qsTr" && word != "tr") || i >= code.size() || code[i] != '(')
                    continue;
                ++i;
                skipSpace();
                if (i >= code.size() || (code[i] != '"' && code[i] != '\'')) {
                    *problems << info.fileName() + ": not a literal: " + code.mid(start, 40);
                    continue;
                }
                QString text;
                do {
                    literal(&text);
                    skipSpace();
                } while (i < code.size() && code[i] == '"');
                // The arguments after the text, to the call's closing parenthesis.
                int depth = 0, commas = 0;
                while (i < code.size()) {
                    const QChar d = code[i];
                    if (d == '"' || d == '\'') {
                        QString skipped;
                        literal(&skipped);
                        continue;
                    }
                    if (d == '(' || d == '[' || d == '{')
                        ++depth;
                    else if ((d == ')' || d == ']' || d == '}') && depth-- == 0)
                        break;
                    else if (d == ',' && depth == 0)
                        ++commas;
                    ++i;
                }
                found[text] = found.value(text) || commas >= 2;
            } else {
                ++i;
            }
        }
    }
    return found;
}

// An item by name among what is drawn under another; a popup's content is
// not among its window's QObject children.
QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (!item || item->objectName() == name)
        return item;
    for (QQuickItem *child : item->childItems())
        if (QQuickItem *found = visualChild(child, name))
            return found;
    return nullptr;
}

} // namespace

class SlateTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QQuickStyle::setStyle("Material"); }

    void shapesAreHitByTheirOutline() {
        Board board;
        const int rect = board.add("rect", 0, 0, 100, 60);
        const int circle = board.add("ellipse", 200, 0, 100, 100);
        const int triangle = board.add("triangle", 400, 0, 100, 100);
        QCOMPARE(board.count(), 3);
        QCOMPARE(board.itemAt(50, 30, 0), rect);
        QCOMPARE(board.itemAt(150, 30, 0), 0);
        // The corner of a circle's box is outside the circle, and the top
        // corner of a triangle's box outside the triangle.
        QCOMPARE(board.itemAt(250, 50, 0), circle);
        QCOMPARE(board.itemAt(203, 3, 0), 0);
        QCOMPARE(board.itemAt(450, 80, 0), triangle);
        QCOMPARE(board.itemAt(405, 10, 0), 0);

        const int line = board.add("line", 0, 200, 100, 0);
        QCOMPARE(board.itemAt(50, 203, 2), line);
        QCOMPARE(board.itemAt(50, 230, 2), 0);
        // A level line still has bounds a selection rectangle can cross.
        board.selectInRect(40, 150, 20, 100, false);
        QVERIFY(board.isSelected(line));
    }

    void connectionsFollowTheirShapes() {
        Board board;
        const int a = board.add("rect", 0, 0, 100, 100);
        const int b = board.add("rect", 300, 0, 100, 100);
        const int connector = board.link(a, b);
        QVERIFY(connector > 0);
        // Asking again, either way round, gives the same connection.
        QCOMPARE(board.link(b, a), connector);
        QCOMPARE(board.link(a, a), 0);
        QCOMPARE(board.count(), 3);

        // It runs between the facing edges, a little short of each.
        QLineF line = board.line(*board.find(connector));
        QCOMPARE(line.p1(), QPointF(105, 50));
        QCOMPARE(line.p2(), QPointF(295, 50));

        QSignalSpy changes(&board, &QAbstractItemModel::dataChanged);
        board.selectOnly(b);
        changes.clear();
        board.moveSelection(0, 400);
        line = board.line(*board.find(connector));
        // Now b is below and to the right: the connector leaves a's corner
        // side and arrives at b's top.
        QVERIFY(line.p2().y() < 400);
        QVERIFY(line.p2().y() > 380);
        QVERIFY(line.p1().y() > 50);
        // The moved shape and its connector both told the view.
        QCOMPARE(changes.count(), 2);

        // A circle is met on its outline, not its box.
        const int circle = board.add("ellipse", 0, 300, 100, 100);
        const QPointF edge = board.edgePoint(circle, 150, 450);
        QVERIFY(std::abs(std::hypot(edge.x() - 50, edge.y() - 350) - 55) < 0.001);
    }

    void turnedShapesAreHitWhereTheyAreDrawn() {
        Board board;
        // A wide bar, stood on its end about its middle.
        const int bar = board.add("rect", 0, 0, 100, 20);
        board.setRotation(bar, 90);
        QCOMPARE(board.itemAt(50, 50, 0), bar);
        QCOMPARE(board.itemAt(50, -35, 0), bar);
        QCOMPARE(board.itemAt(5, 10, 0), 0);
        const QRectF upright = board.boundsOf(bar);
        QVERIFY(std::abs(upright.x() - 40) < 1e-9 && std::abs(upright.y() + 40) < 1e-9);
        QVERIFY(std::abs(upright.width() - 20) < 1e-9 && std::abs(upright.height() - 100) < 1e-9);

        // A connection meets the bar where it now stands: on its long side.
        const int other = board.add("postit", 300, -65, 150, 150);
        const QLineF line = board.line(*board.find(board.link(bar, other)));
        QVERIFY(std::abs(line.p1().x() - 65) < 1e-6);
        QVERIFY(std::abs(line.p1().y() - 10) < 1e-6);

        // A full turn is no turn, the keys turn every selected shape about
        // its own middle, and one undo puts them back.
        board.setRotation(bar, 450);
        QCOMPARE(board.find(bar)->rotation, 90.0);
        board.selectAll();
        board.rotateSelection(15);
        QCOMPARE(board.find(bar)->rotation, 105.0);
        QCOMPARE(board.find(other)->rotation, 15.0);
        QCOMPARE(board.find(other)->x, 300.0);
        board.undo();
        QCOMPARE(board.find(other)->rotation, 0.0);

        // And it is saved with the board.
        Board again;
        again.load(board.toJson());
        QCOMPARE(again.find(bar)->rotation, 90.0);
        QCOMPARE(again.find(other)->kind, QString("postit"));
    }

    void selectedShapesLineUpOnOneSide() {
        Board board;
        const int a = board.add("rect", 0, 0, 100, 60);
        const int b = board.add("ellipse", 250, 40, 80, 80);
        // A bar stood on its end: it lines up by where it is seen to be.
        const int c = board.add("rect", 400, 100, 100, 20);
        board.setRotation(c, 90);
        const int line = board.add("line", 600, 300, -50, 0);
        const int connector = board.link(a, b);

        // One thing alone has nothing to line up with.
        board.selectOnly(b);
        board.alignSelection("left");
        QCOMPARE(board.find(b)->x, 250.0);

        board.selectAll();
        board.alignSelection("left");
        for (int id : {a, b, c, line})
            QVERIFY(std::abs(board.boundsOf(id).left()) < 1e-9);
        QCOMPARE(board.find(b)->y, 40.0);
        // The connector still joins them where they now are.
        QCOMPARE(board.line(*board.find(connector)).p2(), board.edgePoint(b, 50, 30));

        board.alignSelection("bottom");
        for (int id : {a, b, c, line})
            QVERIFY(std::abs(board.boundsOf(id).bottom() - 300) < 1e-9);
        // The tallest of them, the bar, sets where the top is.
        board.alignSelection("top");
        for (int id : {a, b, c, line})
            QVERIFY(std::abs(board.boundsOf(id).top() - 200) < 1e-9);
        board.alignSelection("right");
        QVERIFY(std::abs(board.boundsOf(b).right() - board.boundsOf(a).right()) < 1e-9);

        // Centered on each other: every middle on the one line, across and
        // then up and down, the turned bar by where it is seen like the rest.
        board.alignSelection("center");
        const double centerX = board.selectionBounds().center().x();
        for (int id : {a, b, c, line})
            QVERIFY(std::abs(board.boundsOf(id).center().x() - centerX) < 1e-9);
        QVERIFY(std::abs(board.boundsOf(a).top() - 200) > 1e-9 || std::abs(board.boundsOf(c).top() - 200) < 1e-9);
        board.alignSelection("middle");
        const double centerY = board.selectionBounds().center().y();
        for (int id : {a, b, c, line})
            QVERIFY(std::abs(board.boundsOf(id).center().y() - centerY) < 1e-9);
        // Centering across left the heights alone, and the other way round.
        QVERIFY(std::abs(board.boundsOf(a).center().x() - centerX) < 1e-9);
        board.undo();
        board.undo();

        // Each alignment is one step back.
        board.undo();
        board.undo();
        board.undo();
        board.undo();
        QCOMPARE(board.find(b)->x, 250.0);
        QCOMPARE(board.find(line)->y, 300.0);
    }

    void aDraggedSelectionCatchesOnWhatItPasses() {
        Board board;
        board.add("rect", 0, 0, 100, 100);
        board.add("rect", 400, 40, 60, 60);
        const int moving = board.add("ellipse", 103, 300, 50, 50);
        QCOMPARE(board.movableBounds(), QRectF(103, 300, 50, 50));

        // Three to the right of the first one's right side: it catches there,
        // and the guide runs down that side from the top of the one to the
        // bottom of the other. Nothing is near it up and down.
        QVariantMap snap = board.snapOffset(103, 300, 50, 50, 6);
        QCOMPARE(snap.value("dx").toDouble(), -3.0);
        QCOMPARE(snap.value("dy").toDouble(), 0.0);
        QVariantList guides = snap.value("guides").toList();
        QCOMPARE(guides.size(), 1);
        QCOMPARE(guides.first().toMap().value("x1").toDouble(), 100.0);
        QCOMPARE(guides.first().toMap().value("x2").toDouble(), 100.0);
        QCOMPARE(guides.first().toMap().value("y1").toDouble(), 0.0);
        QCOMPARE(guides.first().toMap().value("y2").toDouble(), 350.0);

        // Middles catch on middles, and both ways at once: its middle on the
        // first one's across, and on the second one's up and down.
        snap = board.snapOffset(27, 47, 50, 50, 6);
        QCOMPARE(snap.value("dx").toDouble(), -2.0);
        QCOMPARE(snap.value("dy").toDouble(), -2.0);
        // Its top and bottom now also meet... nothing else: one line each way.
        guides = snap.value("guides").toList();
        QCOMPARE(guides.size(), 2);

        // Out of reach of everything, it goes where it is put, with no guides.
        snap = board.snapOffset(200, 300, 50, 50, 6);
        QCOMPARE(snap.value("dx").toDouble(), 0.0);
        QCOMPARE(snap.value("dy").toDouble(), 0.0);
        QVERIFY(snap.value("guides").toList().isEmpty());

        // It never catches on itself or on the rest of what is being moved.
        board.selectAll();
        QVERIFY(board.snapOffset(1, 1, 460, 350, 6).value("guides").toList().isEmpty());
        QVERIFY(board.find(moving));
    }

    void aResizedSideCatchesOnWhatIsAround() {
        Board board;
        board.add("rect", 300, 0, 100, 210);
        board.add("ellipse", 0, 400, 180, 80);
        const int resized = board.add("rect", 0, 0, 160, 100);
        QVERIFY(board.isSelected(resized));

        // Its bottom side, dragged to four short of the tall one's bottom,
        // ends level with it; the guide runs along that line under both.
        QVariantMap snap = board.snapEdges(0, 206, false, true, 6);
        QCOMPARE(snap.value("dy").toDouble(), 4.0);
        QCOMPARE(snap.value("dx").toDouble(), 0.0);
        QVariantList guides = snap.value("guides").toList();
        QCOMPARE(guides.size(), 1);
        QCOMPARE(guides.first().toMap().value("y1").toDouble(), 210.0);
        QCOMPARE(guides.first().toMap().value("x1").toDouble(), 0.0);
        QCOMPARE(guides.first().toMap().value("x2").toDouble(), 400.0);

        // A corner catches both ways at once: on the wide one's right side
        // and on the tall one's middle.
        snap = board.snapEdges(183, 103, true, true, 6);
        QCOMPARE(snap.value("dx").toDouble(), -3.0);
        QCOMPARE(snap.value("dy").toDouble(), 2.0);
        QCOMPARE(snap.value("guides").toList().size(), 2);

        // A side only catches the way it moves, and only within reach.
        snap = board.snapEdges(183, 103, false, true, 6);
        QCOMPARE(snap.value("dx").toDouble(), 0.0);
        QCOMPARE(snap.value("guides").toList().size(), 1);
        QVERIFY(board.snapEdges(240, 150, true, true, 6).value("guides").toList().isEmpty());
    }

    void severalThingsAreSizedTogether() {
        Board board;
        const int box = board.add("rect", 0, 0, 100, 50);
        const int bar = board.add("rect", 200, 0, 100, 20);
        board.setRotation(bar, 90);
        const int line = board.add("line", 0, 100, 100, 0);
        const int words = board.add("text", 200, 100, 40, 20);
        board.setFontSize(words, 20);
        const int connector = board.link(box, bar);
        board.selectAll();
        QCOMPARE(board.groupCount(), 4);
        const QRectF from = board.movableBounds();

        // Twice as wide, as tall as before.
        board.beginScale();
        board.scaleSelectionTo(from.x(), from.y(), from.width() * 2, from.height());
        QCOMPARE(board.find(box)->w, 200.0);
        QCOMPARE(board.find(box)->h, 50.0);
        // The bar stands on its end, so its own width runs up and down and
        // stays; its thickness, across, doubles.
        QCOMPARE(board.find(bar)->w, 100.0);
        QCOMPARE(board.find(bar)->h, 40.0);
        QCOMPARE(board.find(line)->w, 200.0);
        QCOMPARE(board.find(line)->h, 0.0);
        // The box around them is twice as wide, from the same left.
        const QRectF to = board.movableBounds();
        QVERIFY(std::abs(to.left() - from.left()) < 1e-6);
        QVERIFY(std::abs(to.width() - from.width() * 2) < 1e-6);
        // Each step works from where things were, so going back is exact.
        board.scaleSelectionTo(from.x(), from.y(), from.width(), from.height());
        QCOMPARE(board.find(box)->w, 100.0);
        QCOMPARE(board.find(bar)->h, 20.0);
        // Text grows its type, evenly.
        board.scaleSelectionTo(from.x(), from.y(), from.width() * 2, from.height() * 2);
        QCOMPARE(board.find(words)->fontSize, 40);
        QVERIFY(board.find(connector));
    }

    void iconsArePlacedAndKept() {
        Board board;
        QVERIFY(board.iconNames().size() >= 20);
        for (const QString &name : board.iconNames())
            QVERIFY2(!board.iconPath(name).isEmpty(), qPrintable(name));
        QCOMPARE(board.addIcon("unicorn", 0, 0, 64, 64), 0);

        const int heart = board.addIcon("heart", 0, 0, 64, 64);
        QCOMPARE(board.find(heart)->kind, QString("icon"));
        QCOMPARE(board.find(heart)->icon, QString("heart"));
        QVERIFY(board.isSelected(heart));
        // Like any shape: colored, connected, hit inside, sized from its sides.
        board.setSelectionColor("red");
        QCOMPARE(board.find(heart)->color, QString("red"));
        const int box = board.add("rect", 200, 0, 100, 100);
        QVERIFY(board.link(heart, box) > 0);
        QCOMPARE(board.itemAt(32, 32, 0), heart);
        QCOMPARE(board.outlineSide(heart, 64, 32, 4), QString("r"));

        // Kept with the board, copied with the clipboard; an icon this
        // Slate doesn't know is shown as a question rather than as nothing.
        Board again;
        again.load(board.toJson());
        QCOMPARE(again.find(heart)->icon, QString("heart"));
        board.selectOnly(heart);
        board.copy();
        QVERIFY(board.paste(500, 500));
        QCOMPARE(board.items().last().icon, QString("heart"));
        again.load(QJsonObject{{"items", QJsonArray{QJsonObject{{"id", 1}, {"kind", "icon"}, {"icon", "unicorn"},
                                                                {"w", 10}, {"h", 10}}}}});
        QCOMPARE(again.find(1)->icon, QString("question"));
    }

    void shapesComeToTheFrontAndGoToTheBack() {
        Board board;
        const int under = board.add("rect", 0, 0, 100, 100);
        const int middle = board.add("rect", 20, 20, 100, 100);
        const int over = board.add("rect", 40, 40, 100, 100);
        const int connector = board.link(under, over);
        QCOMPARE(board.itemAt(60, 60, 0), over);

        board.selectOnly(under);
        board.bringToFront();
        QCOMPARE(board.itemAt(60, 60, 0), under);
        QCOMPARE(board.items().last().id, under);
        QVERIFY(board.isSelected(under));
        QVERIFY(board.find(connector));
        // Already there: nothing to do, and nothing to undo.
        board.bringToFront();

        board.selectOnly(over);
        board.toggle(under);
        board.sendToBack();
        // The two keep the order they had between them.
        QCOMPARE(board.items().at(0).id, over);
        QCOMPARE(board.items().at(1).id, under);
        QCOMPARE(board.itemAt(60, 60, 0), middle);

        board.undo();
        QCOMPARE(board.itemAt(60, 60, 0), under);
        board.undo();
        QCOMPARE(board.itemAt(60, 60, 0), over);
    }

    void deletingAShapeDeletesItsConnections() {
        Board board;
        const int a = board.add("rect", 0, 0, 100, 100);
        const int b = board.add("ellipse", 300, 0, 100, 100);
        const int c = board.add("triangle", 600, 0, 100, 100);
        board.link(a, b);
        const int kept = board.link(b, c);
        board.selectOnly(a);
        board.removeSelection();
        QCOMPARE(board.count(), 3);
        QVERIFY(board.find(kept));
        QVERIFY(!board.find(a));
        QCOMPARE(board.selectionCount(), 0);
    }

    void undoStepsBackThroughWholeGestures() {
        Board board;
        const int a = board.add("rect", 0, 0, 100, 100);
        // A drag is many small moves and one step to undo.
        board.checkpoint();
        for (int i = 0; i < 10; ++i)
            board.moveSelection(5, 0);
        QCOMPARE(board.find(a)->x, 50.0);
        // A gesture that changed nothing isn't a step.
        board.checkpoint();
        board.undo();
        QCOMPARE(board.find(a)->x, 0.0);
        board.redo();
        QCOMPARE(board.find(a)->x, 50.0);
        board.undo();
        board.undo();
        QCOMPARE(board.count(), 0);
        QVERIFY(!board.canUndo());
        board.redo();
        board.redo();
        QCOMPARE(board.find(a)->x, 50.0);
    }

    void pastedShapesKeepTheirConnections() {
        Board board;
        const int a = board.add("rect", 0, 0, 100, 100);
        const int b = board.add("rect", 200, 0, 100, 100);
        board.link(a, b);
        board.selectAll();
        board.copy();
        QVERIFY(board.paste(1000, 1000));
        QCOMPARE(board.count(), 6);
        QCOMPARE(board.selectionCount(), 2);
        // The copies are centered on the point, and joined to each other.
        QCOMPARE(board.selectionBounds().center(), QPointF(1000, 1000));
        const BoardItem &connector = board.items().last();
        QVERIFY(connector.isConnector());
        QVERIFY(board.isSelected(connector.from) && board.isSelected(connector.to));
        QVERIFY(connector.from != a && connector.to != b);

        QVERIFY(board.duplicate(0, 0, false));
        QCOMPARE(board.count(), 9);
    }

    void colorsAreThemeKeys() {
        Board board;
        const int a = board.add("rect", 0, 0, 100, 100);
        board.setSelectionColor("blue");
        QCOMPARE(board.find(a)->color, QString("blue"));
        // New shapes take the color last picked; anything else is no color.
        QCOMPARE(board.find(board.add("ellipse", 0, 0, 10, 10))->color, QString("blue"));
        board.setSelectionColor("#123456");
        QCOMPARE(board.color(), QString());

        // The menu marks the color of what is selected, not the one new
        // shapes take, and says when the selection has more than one.
        Board marked;
        QSignalSpy shown(&marked, &Board::shownColorChanged);
        const int red = marked.add("rect", 0, 0, 10, 10);
        marked.setSelectionColor("red");
        const int green = marked.add("rect", 20, 0, 10, 10);
        marked.setSelectionColor("green");
        QCOMPARE(marked.color(), QString("green"));
        marked.selectOnly(red);
        QCOMPARE(marked.shownColor(), QString("red"));
        QVERIFY(shown.count() > 0);
        marked.toggle(green);
        QCOMPARE(marked.shownColor(), QString("mixed"));
        marked.setSelectionColor("blue");
        QCOMPARE(marked.shownColor(), QString("blue"));
        // With nothing selected it is the color new shapes will take.
        marked.clearSelection();
        QCOMPARE(marked.shownColor(), QString("blue"));
        marked.setSelectionColor("");
        marked.selectOnly(red);
        QCOMPARE(marked.shownColor(), QString("blue"));
        marked.undo();
        // Undoing the recolor brings the red back, and the menu with it.
        QCOMPARE(marked.shownColor(), QString("red"));

        // A post-it is yellow paper unless it is given another color, and is
        // marked as yellow, never as having none: when it is made, when "no
        // color" is picked for it, and when a board saved before it was
        // stored that way is opened.
        Board notes;
        const int note = notes.add("postit", 0, 0, 100, 100);
        QCOMPARE(notes.find(note)->color, QString("yellow"));
        QCOMPARE(notes.shownColor(), QString("yellow"));
        notes.setSelectionColor("green");
        notes.setSelectionColor("");
        QCOMPARE(notes.find(note)->color, QString("yellow"));
        const int plain = notes.add("rect", 200, 0, 50, 50);
        QCOMPARE(notes.find(plain)->color, QString());
        notes.load(QJsonObject{{"items", QJsonArray{QJsonObject{{"id", 1}, {"kind", "postit"}, {"x", 0}, {"y", 0},
                                                                {"w", 100}, {"h", 100}}}}});
        notes.selectOnly(1);
        QCOMPARE(notes.shownColor(), QString("yellow"));
    }

    void boardsSurviveARestart() {
        QTemporaryDir dir;
        QString first, second;
        {
            Backend backend(dir.path());
            QCOMPARE(backend.boards().size(), 1);
            first = backend.currentId();
            QCOMPARE(backend.currentName(), QString("Board 1"));
            Board *board = backend.board();
            const int a = board->add("rect", 10, 20, 100, 60);
            const int b = board->add("text", 300, 20, 0, 0);
            board->setText(a, "Plan");
            board->setText(b, "Ship it");
            board->link(a, b);
            board->setView(40, 50, 1.5);
            backend.rename(first, "  Roadmap ");

            backend.newBoard();
            second = backend.currentId();
            QCOMPARE(backend.board()->count(), 0);
            backend.board()->add("ellipse", 0, 0, 50, 50);
            backend.open(first);
            QCOMPARE(backend.board()->count(), 3);
        }

        Backend backend(dir.path());
        QCOMPARE(backend.boards().size(), 2);
        // It opens where it was left.
        QCOMPARE(backend.currentId(), first);
        QCOMPARE(backend.currentName(), QString("Roadmap"));
        const Board *board = backend.board();
        QCOMPARE(board->count(), 3);
        QCOMPARE(board->items().at(0).text, QString("Plan"));
        QVERIFY(board->items().at(2).isConnector());
        QVERIFY(board->hasView());
        QCOMPARE(board->viewZoom(), 1.5);
        backend.next();
        QCOMPARE(backend.currentId(), second);
        QCOMPARE(backend.board()->count(), 1);
    }

    void aBoardIsSavedAsPdfAndSvg() {
        QTemporaryDir dir, downloads;
        Backend backend(dir.path());
        backend.setExportDirectory(downloads.path());
        backend.rename(backend.currentId(), "Road/map");
        Board *board = backend.board();
        const QVariantMap colors{{"blue", QColor("#7aa2f7")}, {"yellow", QColor("#e0af68")}};
        auto save = [&](const QString &format) {
            return backend.exportBoard(format, colors, QColor("#0e0e10"), QColor("#e6e6e9"), false, 6);
        };

        // There is nothing to save of an empty board.
        QVERIFY(!save("pdf").value("ok").toBool());
        QVERIFY(QDir(downloads.path()).isEmpty());

        const int plan = board->add("rect", 0, 0, 160, 100);
        board->setText(plan, "Plan");
        board->setSelectionColor("blue");
        const int note = board->add("postit", 300, 0, 150, 150);
        board->setText(note, "Call Pat");
        board->setRotation(note, 12);
        board->add("ellipse", 0, 300, 120, 120);
        board->add("triangle", 300, 300, 120, 110);
        board->add("line", 0, 500, 200, 40);
        const int words = board->add("text", 300, 500, 90, 30);
        board->setText(words, "Loose words");
        board->link(plan, note);
        board->addIcon("star", 500, 300, 64, 64);

        // Named after the board, with what a file name can't hold taken out.
        QVariantMap result = save("pdf");
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("path").toString(), downloads.path() + "/Road-map.pdf");
        QFile pdf(result.value("path").toString());
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().startsWith("%PDF"));

        result = save("svg");
        QVERIFY(result.value("ok").toBool());
        QFile svg(result.value("path").toString());
        QVERIFY(svg.open(QIODevice::ReadOnly));
        const QByteArray drawing = svg.readAll();
        QVERIFY(drawing.contains("<svg"));
        // The words are in it as words, and the shape's color as its color.
        QVERIFY(drawing.contains("Plan"));
        QVERIFY(drawing.contains("Call Pat"));
        QVERIFY(drawing.contains("Loose words"));
        QVERIFY(drawing.toLower().contains("#7aa2f7"));

        // A second export never writes over the first.
        result = save("svg");
        QCOMPARE(result.value("path").toString(), downloads.path() + "/Road-map 2.svg");
        QVERIFY(QFile::exists(downloads.path() + "/Road-map.svg"));

        // Only what it can write, and only where it can: otherwise it says
        // so and leaves nothing behind.
        QVERIFY(!save("png").value("ok").toBool());
        QVERIFY(QFile::setPermissions(downloads.path(), QFile::ReadOwner | QFile::ExeOwner));
        result = save("pdf");
        QVERIFY(!result.value("ok").toBool());
        QVERIFY(QFile::setPermissions(downloads.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(!QFile::exists(result.value("path").toString()));
        QCOMPARE(QDir(downloads.path()).entryList(QDir::Files).size(), 3);
    }

    void deletingTheLastBoardLeavesAnEmptyOne() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        const QString first = backend.currentId();
        backend.board()->add("rect", 0, 0, 10, 10);
        backend.remove(first);
        QCOMPARE(backend.boards().size(), 1);
        QVERIFY(backend.currentId() != first);
        QCOMPARE(backend.board()->count(), 0);
        QVERIFY(!QFile::exists(dir.path() + "/" + first + ".json"));
    }

    void aFileThatIsNotABoardIsLeftAlone() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/notes.json", "not json at all"));
        Backend backend(dir.path());
        QCOMPARE(backend.boards().size(), 1);
        QVERIFY(backend.currentId() != QString("notes"));
        backend.save();
        QFile file(dir.path() + "/notes.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("not json at all"));
    }

    void aFailedSaveIsReported() {
        QTemporaryDir dir;
        Backend backend(dir.path());
        QCOMPARE(backend.saveError(), QString());
        // No temp file can be written beside the board, so the save fails
        // rather than rewriting the board in place.
        QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner));
        backend.board()->add("rect", 0, 0, 10, 10);
        QVERIFY(!backend.save());
        QVERIFY(backend.saveError().endsWith(backend.currentId() + ".json"));
        QVERIFY(QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(backend.save());
        QCOMPARE(backend.saveError(), QString());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QColor("black"));
        QVERIFY(!theme.light());
        QCOMPARE(theme.radius(), 0);
    }

    void themeReadsThePalette() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("theme"));
        QVERIFY(write(dir.path() + "/theme/colors.toml", "mode = \"light\"\n"
                                                         "theme_type = \"dark\"\n"
                                                         "accent = \"#112233\"\n"
                                                         "bg = #fafafa # paper\n"
                                                         "foreground = '#202020'\n"
                                                         "color1 = \"#aa0000\"\n"));

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QColor("#112233"));
        QCOMPARE(theme.accentForeground(), QColor("white"));
        QCOMPARE(theme.background(), QColor("#fafafa"));
        QCOMPARE(theme.foreground(), QColor("#202020"));
        QCOMPARE(theme.colors().value("red").value<QColor>(), QColor("#aa0000"));
        QVERIFY(theme.light());
    }

    void themeAsksHyprlandAgainOnlyWhenItReloads() {
        // The fake Hyprland lives outside the theme directory, whose changes
        // the Theme also follows.
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));

        // Once on start, and once more when the event socket connects.
        Theme theme(dir.path(), nullptr, command, events.fullServerName());
        QTRY_VERIFY(events.hasPendingConnections());
        QLocalSocket *hyprland = events.nextPendingConnection();
        QTRY_COMPARE(lines(hypr.filePath("calls")), 2);
        QTRY_COMPARE(theme.radius(), 4);

        // The second call has counted itself before it reads its answer; let
        // it finish, or it reads the answer meant for the third.
        QTest::qWait(200);
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":10}"));
        QTest::qWait(300);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        // Events arrive in pieces; only a whole configreloaded line counts.
        hyprland->write("activewindow>>kitty,~\nconfigrel");
        hyprland->flush();
        QTest::qWait(100);
        QCOMPARE(lines(hypr.filePath("calls")), 2);
        hyprland->write("oaded>>\n");
        QTRY_COMPARE(theme.radius(), 10);
        QCOMPARE(lines(hypr.filePath("calls")), 3);
    }

    void themeReachesHyprlandOnceItListens() {
        QTemporaryDir dir, hypr;
        const QString command = fakeHyprctl(hypr.path());
        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":4}"));
        Theme theme(dir.path(), nullptr, command, hypr.filePath("events.sock"));
        QTRY_COMPARE(theme.radius(), 4);

        QVERIFY(write(hypr.filePath("response.json"), "{\"int\":8}"));
        QLocalServer events;
        QVERIFY(events.listen(hypr.filePath("events.sock")));
        QTRY_VERIFY_WITH_TIMEOUT(events.hasPendingConnections(), 5000);
        QTRY_COMPARE(theme.radius(), 8);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "pt-BR\n"));
        QVERIFY(write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})"));
        QVERIFY(write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorSeesAFirstPick() {
        // Before the first pick there is no ~/.config/omarchy to watch.
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/da.json", R"({"Save": "Gem"})"));
        Translator translator(dir.path() + "/omarchy/language", dir.path());
        QVERIFY(QDir(dir.path()).mkpath("omarchy"));
        QVERIFY(write(dir.path() + "/omarchy/language", "da\n"));
        QTRY_COMPARE(translator.translate("Main", "Save"), QString("Gem"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "ru\n"));
        QVERIFY(write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"},
                                                   "%n files": {"one": "%n file", "other": "%n files"}})"));
        QVERIFY(write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        // An untranslated counted string falls back to English, with English rules.
        QCOMPARE(translator.translate("Main", "%n files", nullptr, 21), QString("%n files"));
    }

    void englishCatalogListsEveryString() {
        // en.json is what translators work from: every text the sources
        // translate is in it, as itself or, when counted, as its English
        // plural forms, and nothing else is.
        const QString sources = QFileInfo(QFINDTESTDATA("../src/Main.qml")).absolutePath();
        QStringList problems;
        const QMap<QString, bool> used = translatables(sources, &problems);
        const QJsonObject english = readCatalog(sources + "/i18n/en.json");
        for (auto it = used.begin(); it != used.end(); ++it) {
            const QJsonValue value = english.value(it.key());
            if (value.isUndefined())
                problems << "missing from en.json: " + it.key();
            else if (it.value() && (value.toObject().keys() != QStringList{"one", "other"}
                                    || value.toObject().value("other").toString() != it.key()))
                problems << "counted, so needs its one and other forms: " + it.key();
            else if (!it.value() && value.toString() != it.key())
                problems << "should be itself: " + it.key();
        }
        for (const QString &key : english.keys())
            if (!used.contains(key))
                problems << "in en.json but no source translates it: " + key;
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void catalogsMatchTheEnglishOne() {
        // A catalog may lack a string (it shows in English meanwhile), but what
        // it has translates something in en.json, with the same placeholders,
        // and a counted string has every form its language uses.
        const QString folder = QFileInfo(QFINDTESTDATA("../src/i18n/en.json")).absolutePath();
        const QJsonObject english = readCatalog(folder + "/en.json");
        QStringList problems;
        for (const QFileInfo &file : QDir(folder).entryInfoList({"*.json"}, QDir::Files)) {
            const QString code = file.completeBaseName();
            const QJsonObject catalog = readCatalog(file.filePath());
            if (catalog.isEmpty())
                problems << code + ": not a JSON object, or empty";
            // Some languages reach a form only at a million ("many" in French).
            QSet<QString> categories{"other", Translator::pluralCategory(code, 1000000)};
            for (long long n = 0; n <= 1000; ++n)
                categories << Translator::pluralCategory(code, n);
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                const QString where = code + ": " + it.key() + ": ";
                const QJsonValue source = english.value(it.key());
                if (source.isUndefined()) {
                    problems << where + "not in en.json";
                } else if (source.isString()) {
                    if (!it.value().isString() || it.value().toString().isEmpty())
                        problems << where + "needs a translation";
                    else if (placeholders(it.value().toString()) != placeholders(it.key()))
                        problems << where + "has other placeholders than the English";
                } else {
                    const QJsonObject forms = it.value().toObject();
                    const QStringList wanted = placeholders(source.toObject().value("other").toString());
                    for (const QString &category : categories)
                        if (forms.value(category).toString().isEmpty())
                            problems << where + "needs its " + category + " form";
                    // A form other than "other" may say its number in words.
                    for (auto form = forms.begin(); form != forms.end(); ++form) {
                        const QStringList has = placeholders(form.value().toString());
                        QStringList needed = wanted;
                        if (form.key() != "other")
                            needed.removeIf([&](const QString &p) { return p.endsWith('n') && !has.contains(p); });
                        if (has != needed)
                            problems << where + form.key() + " has other placeholders than the English";
                    }
                }
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }

    void windowDrawsConnectsAndMoves() {
        // A temp language, theme, and board folder, so the test never
        // depends on the desktop's or touches the user's boards.
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language");
        QCoreApplication::installTranslator(&translator);
        Theme theme(dir.path());
        Backend backend(dir.path() + "/boards");
        backend.setExportDirectory(dir.path() + "/downloads");
        Board *board = backend.board();
        DotGrid::registerType();

        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings << error.toString();
        });
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("board", board);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        auto *view = window->findChild<QQuickItem *>("view");
        QVERIFY(view);
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 5; ++i)
                QTest::mouseMove(window, from + (to - from) * i / 5);
            QTest::mouseRelease(window, Qt::LeftButton, {}, to);
        };
        auto type = [&](const char *text) {
            for (const char *c = text; *c; ++c)
                QTest::keyClick(window, *c);
        };
        // A new board has its origin in the middle of the window.
        const QPointF origin(view->property("camX").toDouble(), view->property("camY").toDouble());
        QCOMPARE(origin, QPointF(window->width() / 2.0, window->height() / 2.0));
        auto world = [&](QPoint p) { return QPointF(p) - origin; };
        QVERIFY(window->findChild<QQuickItem *>("hint")->isVisible());

        // R, then a drag, draws a rectangle and goes back to selecting.
        QTest::keyClick(window, Qt::Key_R);
        QCOMPARE(view->property("tool").toString(), QString("rect"));
        drag({100, 100}, {260, 200});
        QCOMPARE(board->count(), 1);
        QCOMPARE(view->property("tool").toString(), QString("select"));
        const BoardItem rect = board->items().at(0);
        QCOMPARE(rect.kind, QString("rect"));
        QCOMPARE(QRectF(rect.x, rect.y, rect.w, rect.h), QRectF(world({100, 100}), QSizeF(160, 100)));
        QVERIFY(!window->findChild<QQuickItem *>("hint")->isVisible());

        // Over a side of the selected shape the pointer says it can be
        // resized that way; inside it and away from it, it is the arrow.
        auto cursorAt = [&](QPoint p) {
            QTest::mouseMove(window, p);
            QTest::qWait(20);
            return window->cursor().shape();
        };
        QCOMPARE(cursorAt({260, 150}), Qt::SizeHorCursor);
        QCOMPARE(cursorAt({100, 170}), Qt::SizeHorCursor);
        QCOMPARE(cursorAt({180, 100}), Qt::SizeVerCursor);
        QCOMPARE(cursorAt({180, 200}), Qt::SizeVerCursor);
        // The corners point along their diagonals.
        QCOMPARE(cursorAt({97, 97}), Qt::SizeFDiagCursor);
        QCOMPARE(cursorAt({263, 203}), Qt::SizeFDiagCursor);
        QCOMPARE(cursorAt({263, 97}), Qt::SizeBDiagCursor);
        QCOMPARE(cursorAt({97, 203}), Qt::SizeBDiagCursor);
        // With Alt held, a handle turns the shape, and the pointer says so.
        // It changes as the key goes down, with the mouse where it is.
        QCOMPARE(cursorAt({263, 97}), Qt::SizeBDiagCursor);
        QTest::keyPress(window, Qt::Key_Alt);
        QTRY_COMPARE(window->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(window, Qt::Key_Alt);
        QTRY_COMPARE(window->cursor().shape(), Qt::SizeBDiagCursor);
        QCOMPARE(cursorAt({180, 73}), Qt::ArrowCursor);
        QCOMPARE(cursorAt({180, 150}), Qt::ArrowCursor);
        QCOMPARE(cursorAt({400, 300}), Qt::ArrowCursor);
        // Turned an eighth, a side moves along a diagonal and says so.
        board->setRotation(rect.id, 45);
        const QPointF turnedSide = QPointF(180, 150) + QPointF(80, 0) * std::cos(M_PI / 4) + QPointF(0, 80) * std::sin(M_PI / 4);
        QCOMPARE(cursorAt(turnedSide.toPoint()), Qt::SizeFDiagCursor);
        // The dots that connections are drawn from turn with the shape: the
        // one off the middle of its top is now up and to the right.
        cursorAt({180, 150});
        QVariantList dots = view->property("dotList").toList();
        QCOMPARE(dots.size(), 4);
        const double reach = (50 + 16) * std::sin(M_PI / 4);
        QVERIFY(std::abs(dots.at(0).toPointF().x() - (180 + reach)) < 0.01);
        QVERIFY(std::abs(dots.at(0).toPointF().y() - (150 - reach)) < 0.01);
        // They sit straight out from the handles in the middle of the sides.
        const QVariantList handles = view->property("handleList").toList();
        for (const QVariant &dot : dots) {
            bool beside = false;
            for (const QVariant &handle : handles) {
                const QVariantMap h = handle.toMap();
                beside = beside || (h.contains("edge") && std::abs(std::hypot(h.value("x").toDouble() - dot.toPointF().x(),
                                                                              h.value("y").toDouble() - dot.toPointF().y()) - 13) < 0.01);
            }
            QVERIFY(beside);
        }
        board->setRotation(rect.id, 0);
        cursorAt({180, 151});
        dots = view->property("dotList").toList();
        QCOMPARE(dots.at(0).toPointF(), QPointF(180, 84));

                // A selected shape shows where it can be taken hold of: its four
        // corners and the middle of its four sides, and nothing else.
        QCOMPARE(view->property("handleList").toList().size(), 8);
        // The handle in the middle of the right side, a little outside it,
        // moves that side by exactly as far as it is dragged.
        QCOMPARE(cursorAt({263, 150}), Qt::SizeHorCursor);
        drag({263, 150}, {283, 150});
        QCOMPARE(board->find(rect.id)->w, 180.0);
        QCOMPARE(board->find(rect.id)->h, 100.0);
        QCOMPARE(board->find(rect.id)->x, rect.x);
        drag({180, 203}, {180, 213});
        QCOMPARE(board->find(rect.id)->h, 110.0);
        QCOMPARE(board->find(rect.id)->w, 180.0);
        QTest::keySequence(window, QKeySequence::Undo);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(rect.id)->w, 160.0);
        QCOMPARE(board->find(rect.id)->h, 100.0);

        // A side of the selected shape drags that side alone: the right one
        // out and back, the width with it and nothing else.
        drag({260, 150}, {300, 150});
        QCOMPARE(board->find(rect.id)->w, 200.0);
        QCOMPARE(board->find(rect.id)->h, 100.0);
        QCOMPARE(board->find(rect.id)->x, rect.x);
        drag({300, 150}, {260, 160});
        QCOMPARE(board->find(rect.id)->w, 160.0);
        // The top one moves the top and leaves the bottom where it was.
        drag({180, 100}, {180, 80});
        QCOMPARE(board->find(rect.id)->y, rect.y - 20);
        QCOMPARE(board->find(rect.id)->h, 120.0);
        QCOMPARE(board->find(rect.id)->w, 160.0);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(QRectF(board->find(rect.id)->x, board->find(rect.id)->y, board->find(rect.id)->w,
                        board->find(rect.id)->h), QRectF(world({100, 100}), QSizeF(160, 100)));
        // Inside the shape, a drag still moves it.
        drag({180, 150}, {190, 150});
        QCOMPARE(board->find(rect.id)->x, rect.x + 10);
        QCOMPARE(board->find(rect.id)->w, 160.0);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(rect.id)->x, rect.x);

        // A shape needn't be selected first: its outline shows the resize
        // pointer, and a drag from it selects the shape and resizes it.
        board->clearSelection();
        QCOMPARE(cursorAt({260, 150}), Qt::SizeHorCursor);
        QCOMPARE(cursorAt({180, 200}), Qt::SizeVerCursor);
        QCOMPARE(cursorAt({180, 150}), Qt::ArrowCursor);
        drag({260, 150}, {280, 150});
        QCOMPARE(board->selectedId(), rect.id);
        QCOMPARE(board->find(rect.id)->w, 180.0);
        QCOMPARE(board->find(rect.id)->x, rect.x);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(rect.id)->w, 160.0);

        // O and a click drops a circle around the pointer.
        QTest::keyClick(window, Qt::Key_O);
        QTest::mouseClick(window, Qt::LeftButton, {}, {500, 150});
        QCOMPARE(board->count(), 2);
        const BoardItem circle = board->items().at(1);
        QCOMPARE(circle.kind, QString("ellipse"));
        QCOMPARE(QPointF(circle.x + circle.w / 2, circle.y + circle.h / 2), world({500, 150}));

        // A circle is resized from its outline, not from the corners of the
        // box around it, where there is nothing to take hold of.
        board->clearSelection();
        QCOMPARE(cursorAt({560, 150}), Qt::SizeHorCursor);
        QCOMPARE(cursorAt({500, 90}), Qt::SizeVerCursor);
        QCOMPARE(cursorAt({500, 150}), Qt::ArrowCursor);
        QCOMPARE(cursorAt({556, 94}), Qt::ArrowCursor);
        QCOMPARE(board->outlineSide(circle.id, circle.x + circle.w, circle.y + circle.h / 2, 7), QString("r"));

        // Resizing catches too: the rectangle's bottom, dragged to within a
        // pixel of the circle's, ends level with it, and a guide shows it.
        QTest::mouseClick(window, Qt::LeftButton, {}, {180, 150});
        QCOMPARE(board->selectedId(), rect.id);
        QTest::mousePress(window, Qt::LeftButton, {}, {180, 203});
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(window, QPoint(180, 203 + 2 * i));
        QTest::mouseMove(window, QPoint(180, 214));
        QCOMPARE(board->find(rect.id)->h, 110.0);
        QCOMPARE(board->boundsOf(rect.id).bottom(), board->boundsOf(circle.id).bottom());
        QCOMPARE(view->property("guides").toList().size(), 1);
        // Out of reach it goes where it is put, and the guide goes.
        QTest::mouseMove(window, QPoint(180, 230));
        QCOMPARE(board->find(rect.id)->h, 127.0);
        QVERIFY(view->property("guides").toList().isEmpty());
        QTest::mouseMove(window, QPoint(180, 212));
        QTest::mouseRelease(window, Qt::LeftButton, {}, {180, 212});
        QCOMPARE(board->find(rect.id)->h, 110.0);
        QVERIFY(view->property("guides").toList().isEmpty());
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(rect.id)->h, 100.0);
        board->clearSelection();

        // T is the triangle, and T again is text.
        QTest::keyClick(window, Qt::Key_T);
        QCOMPARE(view->property("tool").toString(), QString("triangle"));
        QTest::keyClick(window, Qt::Key_T);
        QCOMPARE(view->property("tool").toString(), QString("text"));
        QTest::keyClick(window, Qt::Key_L);
        drag({100, 400}, {300, 450});
        QCOMPARE(board->items().at(2).kind, QString("line"));
        QCOMPARE(board->items().at(2).w, 200.0);

        // With Shift held a line keeps straight: level, upright, or on a
        // diagonal, whichever is nearest, and the angle shows as it is drawn.
        // QTest's moves carry no modifiers, so these are sent by hand.
        auto shiftDrag = [&](QPoint from, const QList<QPoint> &path, std::function<void()> during = {}) {
            QTest::mousePress(window, Qt::LeftButton, Qt::ShiftModifier, from);
            for (const QPoint &to : path) {
                QMouseEvent move(QEvent::MouseMove, to, to, window->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
                                 Qt::ShiftModifier);
                QCoreApplication::sendEvent(window, &move);
            }
            if (during)
                during();
            QTest::mouseRelease(window, Qt::LeftButton, Qt::ShiftModifier, path.last());
        };
        auto *lineAngle = window->findChild<QQuickItem *>("angleLabel");
        QTest::keyClick(window, Qt::Key_L);
        shiftDrag({100, 500}, {{140, 505}, {200, 507}, {300, 512}}, [&] {
            QVERIFY(lineAngle->isVisible());
            QCOMPARE(lineAngle->property("text").toString(), QString("0°"));
        });
        const BoardItem level = board->items().last();
        QCOMPARE(level.kind, QString("line"));
        QCOMPARE(level.w, 200.0);
        QCOMPARE(level.h, 0.0);
        QVERIFY(!lineAngle->isVisible());
        QTest::keyClick(window, Qt::Key_L);
        shiftDrag({600, 500}, {{610, 480}, {630, 430}, {650, 420}});
        const BoardItem diagonal = board->items().last();
        QVERIFY(std::abs(diagonal.w + diagonal.h) < 1e-9);
        QVERIFY(diagonal.w > 60);
        // Its end, dragged with Shift, keeps it straight too: now upright.
        const QPoint diagonalEnd = (QPointF(diagonal.x + diagonal.w, diagonal.y + diagonal.h) + origin).toPoint();
        shiftDrag(diagonalEnd, {diagonalEnd + QPoint(5, -10), diagonalEnd + QPoint(-60, -40),
                                diagonalEnd + QPoint(-63, -60)});
        QVERIFY(std::abs(board->find(diagonal.id)->w) < 1e-9);
        QVERIFY(board->find(diagonal.id)->h < -100);
        // Shift keeps a shape's proportions as it is resized. From a corner
        // the way the pointer went furthest sets the size: half as wide
        // again, so half as tall again, from the same top left.
        QTest::mouseClick(window, Qt::LeftButton, {}, {180, 150});
        QCOMPARE(board->selectedId(), rect.id);
        shiftDrag({263, 203}, {{280, 205}, {320, 210}, {343, 213}});
        QCOMPARE(board->find(rect.id)->w, 240.0);
        QCOMPARE(board->find(rect.id)->h, 150.0);
        QCOMPARE(board->find(rect.id)->x, rect.x);
        QCOMPARE(board->find(rect.id)->y, rect.y);
        QTest::keySequence(window, QKeySequence::Undo);
        // From a side, the other way grows with it, evenly about the middle.
        shiftDrag({263, 150}, {{280, 150}, {320, 151}, {343, 150}});
        QCOMPARE(board->find(rect.id)->w, 240.0);
        QCOMPARE(board->find(rect.id)->h, 150.0);
        QCOMPARE(board->find(rect.id)->x, rect.x);
        QCOMPARE(board->find(rect.id)->y, rect.y - 25);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(rect.id)->w, 160.0);
        QCOMPARE(board->find(rect.id)->h, 100.0);

        // Several shapes selected are sized together by the box around them:
        // the rectangle and the circle, half as large again from the box's
        // top left, each where it was within the box, and one step to undo.
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, {500, 150});
        QCOMPARE(board->groupCount(), 2);
        QCOMPARE(view->property("handleList").toList().size(), 8);
        QVERIFY(window->findChild<QQuickItem *>("groupBox")->isVisible());
        const BoardItem round = *board->find(circle.id);
        QTest::mousePress(window, Qt::LeftButton, {}, {563, 213});
        for (const QPoint &p : {QPoint(600, 225), QPoint(700, 250), QPoint(793, 273)})
            QTest::mouseMove(window, p);
        QTest::mouseRelease(window, Qt::LeftButton, {}, {793, 273});
        QCOMPARE(board->find(rect.id)->w, 240.0);
        QCOMPARE(board->find(rect.id)->h, 150.0);
        QCOMPARE(board->find(rect.id)->x, rect.x);
        QCOMPARE(board->find(rect.id)->y, round.y + (rect.y - round.y) * 1.5);
        QCOMPARE(board->find(circle.id)->w, 180.0);
        QCOMPARE(board->find(circle.id)->x, rect.x + (round.x - rect.x) * 1.5);
        QCOMPARE(board->find(circle.id)->y, round.y);
        QCOMPARE(board->groupCount(), 2);
        // Moving them together is as before.
        drag({300, 200}, {320, 200});
        QCOMPARE(board->find(rect.id)->x, rect.x + 20);
        QCOMPARE(board->find(rect.id)->w, 240.0);
        QTest::keySequence(window, QKeySequence::Undo);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(rect.id)->w, 160.0);
        QCOMPARE(board->find(circle.id)->x, round.x);
        board->clearSelection();

        // Without Shift it goes where it is put.
        QTest::keyClick(window, Qt::Key_L);
        drag({100, 560}, {300, 571});
        QCOMPARE(board->items().last().h, 11.0);
        for (int i = 0; i < 4; ++i)
            QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->count(), 3);

        // C, then a drag from one shape to the other, connects them.
        QTest::keyClick(window, Qt::Key_C);
        drag({180, 150}, {500, 150});
        QCOMPARE(board->count(), 4);
        const BoardItem connector = board->items().at(3);
        QVERIFY(connector.isConnector());
        QCOMPARE(connector.from, rect.id);
        QCOMPARE(connector.to, circle.id);
        const QLineF before = board->line(connector);

        // Dragging the circle moves it, and the connection stays on it.
        QTest::keyClick(window, Qt::Key_Escape);
        drag({500, 150}, {500, 300});
        QCOMPARE(board->find(circle.id)->y, circle.y + 150);
        const QLineF after = board->line(*board->find(connector.id));
        QVERIFY(after.p2().y() > before.p2().y() + 100);
        QCOMPARE(board->selectedId(), circle.id);

        // One undo puts the whole drag back.
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(circle.id)->y, circle.y);

        // Dragged to nearly level with the rectangle, the circle catches on
        // it: let go four pixels low, it sits exactly level, and where it was
        // put the other way. A guide shows while it is held there.
        QTest::mousePress(window, Qt::LeftButton, {}, {500, 150});
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(window, QPoint(500 - 8 * i, 150 + 6 * i));
        QTest::mouseMove(window, QPoint(460, 154));
        QCOMPARE(board->find(circle.id)->y, circle.y);
        QCOMPARE(board->find(circle.id)->x, circle.x - 40);
        QVERIFY(view->property("guides").toList().size() >= 1);
        QTest::mouseRelease(window, Qt::LeftButton, {}, {460, 154});
        QVERIFY(view->property("guides").toList().isEmpty());
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(circle.id)->x, circle.x);

        // Enter writes on the selected shape; the keys go to the text, not
        // to the shortcuts.
        QTest::mouseClick(window, Qt::LeftButton, {}, {180, 150});
        QCOMPARE(board->selectedId(), rect.id);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(view->property("editing").toBool());
        type("roq");
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(!view->property("editing").toBool());
        QCOMPARE(board->find(rect.id)->text, QString("roq"));
        QCOMPARE(board->count(), 4);

        // A color key paints the selection.
        QTest::keyClick(window, Qt::Key_7);
        QCOMPARE(board->find(rect.id)->color, QString("blue"));

        // Text is measured by the window and dropped when left empty.
        QTest::keyClick(window, Qt::Key_X);
        QTest::mouseClick(window, Qt::LeftButton, {}, {700, 500});
        QTRY_VERIFY(view->property("editing").toBool());
        QCOMPARE(board->count(), 5);
        type("hello");
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(board->items().last().text, QString("hello"));
        QTRY_VERIFY(board->items().last().w > 20);

        // Clicked, text is selected and its color marked; recolored while it
        // is being written, the words being typed change color with it.
        const int words = board->items().last().id;
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::mouseClick(window, Qt::LeftButton, {}, {710, 500});
        QCOMPARE(board->selectedId(), words);
        QCOMPARE(board->shownColor(), board->find(words)->color);
        board->setSelectionColor("orange");
        QCOMPARE(board->shownColor(), QString("orange"));
        QTest::mouseDClick(window, Qt::LeftButton, {}, {710, 500});
        QTRY_VERIFY(view->property("editing").toBool());
        auto *editor = view->findChild<QQuickItem *>("editor");
        QCOMPARE(editor->property("color").value<QColor>(), theme.colors().value("orange").value<QColor>());
        board->setSelectionColor("cyan");
        QCOMPARE(editor->property("color").value<QColor>(), theme.colors().value("cyan").value<QColor>());
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(board->find(words)->color, QString("cyan"));
        board->setSelectionColor("");
        QTest::mouseDClick(window, Qt::LeftButton, {}, {900, 300});
        QTRY_VERIFY(view->property("editing").toBool());
        QCOMPARE(board->count(), 6);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(board->count(), 5);

        // The wheel zooms on the pointer: what is under it stays under it.
        const QPointF under(300, 200);
        const QPointF anchored = (under - origin) / view->property("zoom").toDouble();
        QWheelEvent wheel(under, window->mapToGlobal(under), {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_COMPARE(view->property("zoom").toDouble(), 1.25);
        const QPointF moved(view->property("camX").toDouble(), view->property("camY").toDouble());
        QVERIFY(std::abs(((under - moved) / 1.25 - anchored).manhattanLength()) < 0.01);
        QTest::keyClick(window, Qt::Key_0);
        QTRY_COMPARE(view->property("zoom").toDouble(), 1.0);

        // Two fingers on a touchpad pan the canvas by exactly what they moved.
        const QPointF rest(view->property("camX").toDouble(), view->property("camY").toDouble());
        for (int i = 0; i < 5; ++i) {
            QWheelEvent pan(under, window->mapToGlobal(under), {6, -4}, {72, -48}, Qt::NoButton, Qt::NoModifier,
                            Qt::ScrollUpdate, false);
            QCoreApplication::sendEvent(window, &pan);
        }
        QCOMPARE(QPointF(view->property("camX").toDouble(), view->property("camY").toDouble()),
                 rest + QPointF(30, -20));
        view->setProperty("camX", origin.x());
        view->setProperty("camY", origin.y());

        // Delete takes the selection, and its connection with it.
        QTest::mouseClick(window, Qt::LeftButton, {}, {180, 150});
        QTest::keyClick(window, Qt::Key_Delete);
        QCOMPARE(board->count(), 3);

        // P and a click sticks a post-it, ready to be written on.
        QTest::keyClick(window, Qt::Key_P);
        QCOMPARE(view->property("tool").toString(), QString("postit"));
        QTest::mouseClick(window, Qt::LeftButton, {}, {1000, 500});
        QCOMPARE(board->count(), 4);
        const int postit = board->items().last().id;
        QCOMPARE(board->find(postit)->kind, QString("postit"));
        QTRY_VERIFY(view->property("editing").toBool());
        type("call pat");
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(board->find(postit)->text, QString("call pat"));
        QCOMPARE(board->selectedId(), postit);

        // Turning catches on every eighth of a turn: taken to within a few
        // degrees of 45, it sits at exactly 45, and the angle beside the
        // pointer says so. In between it turns freely.
        auto *angleLabel = window->findChild<QQuickItem *>("angleLabel");
        QTest::mousePress(window, Qt::LeftButton, Qt::AltModifier, {1078, 422});
        QTest::mouseMove(window, QPoint(1095, 445));
        QTest::mouseMove(window, QPoint(1110, 496));
        QCOMPARE(board->find(postit)->rotation, 45.0);
        QVERIFY(view->property("rotateCaught").toBool());
        QVERIFY(angleLabel->isVisible());
        QCOMPARE(angleLabel->property("text").toString(), QString("45°"));
        QTest::mouseMove(window, QPoint(1106, 472));
        QVERIFY(std::abs(board->find(postit)->rotation - 30.2) < 0.5);
        QVERIFY(!view->property("rotateCaught").toBool());
        // And on the angle another shape is turned to.
        board->setRotation(circle.id, 28);
        QTest::mouseMove(window, QPoint(1106, 471));
        QTest::mouseMove(window, QPoint(1106, 472));
        QCOMPARE(board->find(postit)->rotation, 28.0);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::AltModifier, {1106, 472});
        QVERIFY(!angleLabel->isVisible());
        board->setRotation(circle.id, 0);
        board->setRotation(postit, 0);

        // Alt and a handle turn it, by as much as the handle is taken round
        // the middle: the top right corner down to the bottom right is a
        // quarter turn. The size stays as it was.
        const BoardItem upright = *board->find(postit);
        QTest::mousePress(window, Qt::LeftButton, Qt::AltModifier, {1078, 422});
        for (const QPoint &p : {QPoint(1090, 450), QPoint(1105, 500), QPoint(1090, 550), QPoint(1078, 578)})
            QTest::mouseMove(window, p);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::AltModifier, {1078, 578});
        QCOMPARE(board->find(postit)->rotation, 90.0);
        QCOMPARE(board->find(postit)->w, upright.w);
        QCOMPARE(board->find(postit)->x, upright.x);
        // The handle in the middle of a side does the same: a quarter turn back.
        QTest::mousePress(window, Qt::LeftButton, Qt::AltModifier, {1000, 578});
        for (const QPoint &p : {QPoint(1050, 560), QPoint(1075, 520), QPoint(1078, 500)})
            QTest::mouseMove(window, p);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::AltModifier, {1078, 500});
        QCOMPARE(board->find(postit)->rotation, 0.0);
        QTest::keySequence(window, QKeySequence::Undo);
        QCOMPARE(board->find(postit)->rotation, 90.0);
        // A corner still resizes it, along its own turned sides: the handle
        // now at the bottom right is the one that was at the top right.
        const BoardItem turnedNote = *board->find(postit);
        drag({1078, 578}, {1098, 598});
        QCOMPARE(board->find(postit)->rotation, 90.0);
        QVERIFY(std::abs(board->find(postit)->w - (turnedNote.w + 20)) < 0.01);
        QVERIFY(std::abs(board->find(postit)->h - (turnedNote.h + 20)) < 0.01);
        // A side of a turned shape is dragged where it is seen: the one now
        // on top is its left side, and pulling it up widens the shape while
        // the side across from it stays put.
        const BoardItem widened = *board->find(postit);
        const QRectF seen = board->boundsOf(postit);
        const QPoint topSide = (QPointF(seen.center().x(), seen.top()) + origin).toPoint();
        drag(topSide, topSide - QPoint(0, 30));
        QCOMPARE(board->find(postit)->rotation, 90.0);
        QVERIFY(std::abs(board->find(postit)->w - (widened.w + 30)) < 0.01);
        QVERIFY(std::abs(board->find(postit)->h - widened.h) < 0.01);
        QVERIFY(std::abs(board->boundsOf(postit).bottom() - seen.bottom()) < 0.01);
        QVERIFY(std::abs(board->boundsOf(postit).top() - (seen.top() - 30)) < 0.01);
        QTest::keyClick(window, Qt::Key_Comma);
        QCOMPARE(board->find(postit)->rotation, 75.0);

        // The shapes are behind one button too, which shows the one last
        // used; R, O, T and L still pick them.
        auto *shapeMenu = window->findChild<QObject *>("shapeMenu");
        auto *shapesButton = visualChild(window->contentItem(), "tool-shapes");
        QVERIFY(shapesButton);
        QVERIFY(visualChild(window->contentItem(), "tool-postit"));
        QVERIFY(!visualChild(window->contentItem(), "tool-rect"));
        QTest::mouseClick(window, Qt::LeftButton, {}, shapesButton->mapToScene({21, 21}).toPoint());
        QTRY_VERIFY(shapeMenu->property("opened").toBool());
        auto *triangleRow = visualChild(shapeMenu->property("contentItem").value<QQuickItem *>(), "shape-triangle");
        QVERIFY(triangleRow);
        QTest::mouseClick(window, Qt::LeftButton, {}, triangleRow->mapToScene({30, 16}).toPoint());
        QCOMPARE(view->property("tool").toString(), QString("triangle"));
        QTRY_VERIFY(!shapeMenu->property("visible").toBool());
        QCOMPARE(window->property("lastShape").toString(), QString("triangle"));
        QVERIFY(shapesButton->property("active").toBool());
        QTest::keyClick(window, Qt::Key_O);
        QCOMPARE(window->property("lastShape").toString(), QString("ellipse"));
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!shapesButton->property("active").toBool());
        QCOMPARE(window->property("lastShape").toString(), QString("ellipse"));
        board->selectOnly(postit);

        // The colors are behind one button, and the number keys still pick them.
        auto *colorMenu = window->findChild<QObject *>("colorMenu");
        auto *colorButton = window->findChild<QQuickItem *>("colorButton");
        QTest::mouseClick(window, Qt::LeftButton, {}, colorButton->mapToScene({21, 21}).toPoint());
        QTRY_VERIFY(colorMenu->property("opened").toBool());
        auto *green = visualChild(colorMenu->property("contentItem").value<QQuickItem *>(), "color-green");
        QVERIFY(green);
        QTest::mouseClick(window, Qt::LeftButton, {}, green->mapToScene({20, 16}).toPoint());
        QCOMPARE(board->find(postit)->color, QString("green"));
        QTRY_VERIFY(!colorMenu->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_2);
        QCOMPARE(board->find(postit)->color, QString("red"));

        // Selecting a shape marks its color in the menu: a blue one, then the
        // red post-it, whatever was picked last.
        const int blue = board->add("rect", 2000, 2000, 50, 50);
        board->setSelectionColor("blue");
        board->selectOnly(blue);
        QTest::mouseClick(window, Qt::LeftButton, {}, colorButton->mapToScene({21, 21}).toPoint());
        QTRY_VERIFY(colorMenu->property("opened").toBool());
        auto *menuContent = colorMenu->property("contentItem").value<QQuickItem *>();
        QVERIFY(visualChild(menuContent, "color-blue")->property("current").toBool());
        QVERIFY(!visualChild(menuContent, "color-red")->property("current").toBool());
        board->selectOnly(postit);
        QVERIFY(visualChild(menuContent, "color-red")->property("current").toBool());
        QVERIFY(!visualChild(menuContent, "color-blue")->property("current").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!colorMenu->property("visible").toBool());
        board->removeItem(blue);
        board->selectOnly(postit);

        // Alt and an arrow lines the selection up on that side.
        QTest::keySequence(window, QKeySequence::SelectAll);
        QCOMPARE(board->selectionCount(), 4);
        QTest::keySequence(window, QKeySequence("Alt+Up"));
        for (const BoardItem &item : board->items())
            QVERIFY(std::abs(board->boundsOf(item.id).top() - board->selectionBounds().top()) < 1e-6);

        QTest::keySequence(window, QKeySequence("Alt+H"));
        for (const BoardItem &item : board->items())
            QVERIFY(std::abs(board->boundsOf(item.id).center().x() - board->selectionBounds().center().x()) < 1e-6);
        QTest::keySequence(window, QKeySequence::Undo);

        // The same from the menu, which also puts shapes in front or behind.
        auto *arrangeMenu = window->findChild<QObject *>("arrangeMenu");
        auto *arrangeButton = window->findChild<QQuickItem *>("arrangeButton");
        auto arrange = [&](const QString &name) {
            QTest::mouseClick(window, Qt::LeftButton, {}, arrangeButton->mapToScene({21, 21}).toPoint());
            QTRY_VERIFY(arrangeMenu->property("opened").toBool());
            auto *row = visualChild(arrangeMenu->property("contentItem").value<QQuickItem *>(), "arrange-" + name);
            QVERIFY(row);
            QTest::mouseClick(window, Qt::LeftButton, {}, row->mapToScene({30, 16}).toPoint());
            QTRY_VERIFY(!arrangeMenu->property("visible").toBool());
        };
        arrange("middle");
        for (const BoardItem &item : board->items())
            QVERIFY(std::abs(board->boundsOf(item.id).center().y() - board->selectionBounds().center().y()) < 1e-6);
        arrange("left");
        for (const BoardItem &item : board->items())
            QVERIFY(std::abs(board->boundsOf(item.id).left() - board->selectionBounds().left()) < 1e-6);
        board->selectOnly(board->items().first().id);
        const int first = board->items().first().id;
        arrange("front");
        QCOMPARE(board->items().last().id, first);
        QTest::keyClick(window, Qt::Key_BracketLeft);
        QCOMPARE(board->items().first().id, first);

        // I opens the icons; one picked and a click on the canvas places it
        // there, at a size to see, selected and ready to move or size.
        auto *iconMenu = window->findChild<QObject *>("iconMenu");
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(iconMenu->property("opened").toBool());
        auto *heartCell = visualChild(iconMenu->property("contentItem").value<QQuickItem *>(), "icon-heart");
        QVERIFY(heartCell);
        QTest::mouseClick(window, Qt::LeftButton, {}, heartCell->mapToScene({19, 19}).toPoint());
        QCOMPARE(view->property("tool").toString(), QString("icon"));
        QTRY_VERIFY(!iconMenu->property("visible").toBool());
        QVERIFY(visualChild(window->contentItem(), "tool-icons")->property("active").toBool());
        const int beforeIcon = board->count();
        QTest::mouseClick(window, Qt::LeftButton, {}, {900, 150});
        QCOMPARE(board->count(), beforeIcon + 1);
        const BoardItem placed = board->items().last();
        QCOMPARE(placed.kind, QString("icon"));
        QCOMPARE(placed.icon, QString("heart"));
        QCOMPARE(placed.w, 64.0);
        QCOMPARE(QPointF(placed.x + 32, placed.y + 32), world({900, 150}));
        QCOMPARE(board->selectedId(), placed.id);
        QCOMPARE(view->property("tool").toString(), QString("select"));
        QCOMPARE(view->property("handleList").toList().size(), 8);
        // There is nothing to write on an icon.
        QTest::mouseDClick(window, Qt::LeftButton, {}, {900, 150});
        QVERIFY(!view->property("editing").toBool());
        board->removeItem(placed.id);

        // A second board, and back.
        QTest::keySequence(window, QKeySequence("Ctrl+N"));
        QCOMPARE(backend.boards().size(), 2);
        QCOMPARE(board->count(), 0);
        QTest::keySequence(window, QKeySequence("Ctrl+Tab"));
        QCOMPARE(board->count(), 4);

        // The boards are behind the name at the top left: it shows the one
        // that is open, and its menu picks another or makes a new one.
        auto *boardMenu = window->findChild<QObject *>("boardMenu");
        auto *boardButton = window->findChild<QQuickItem *>("boardButton");
        auto *boardName = window->findChild<QQuickItem *>("boardName");
        auto *zoomLabel = window->findChild<QQuickItem *>("status");
        QCOMPARE(boardName->property("text").toString(), QString("Board 1"));
        QCOMPARE(zoomLabel->property("text").toString(), QString("100%"));
        QVERIFY(boardButton->mapToScene({0, 0}).x() < 40);
        auto pick = [&](const QString &name) {
            QTest::mouseClick(window, Qt::LeftButton, {}, boardButton->mapToScene({20, 17}).toPoint());
            QTRY_VERIFY(boardMenu->property("opened").toBool());
            QQuickItem *row = nullptr;
            QTRY_VERIFY((row = visualChild(boardMenu->property("contentItem").value<QQuickItem *>(), name)));
            QTest::mouseClick(window, Qt::LeftButton, {}, row->mapToScene({40, 17}).toPoint());
            QTRY_VERIFY(!boardMenu->property("visible").toBool());
        };
        pick("board-1");
        QCOMPARE(backend.currentName(), QString("Board 2"));
        QCOMPARE(boardName->property("text").toString(), QString("Board 2"));
        QCOMPARE(board->count(), 0);
        pick("newBoard");
        QCOMPARE(backend.boards().size(), 3);
        QCOMPARE(boardName->property("text").toString(), QString("Board 3"));
        pick("board-0");
        QCOMPARE(backend.currentName(), QString("Board 1"));
        QCOMPARE(board->count(), 4);

        // PDF and SVG, beside the board's name, save it to Downloads and say
        // where it went.
        auto *noticeLabel = window->findChild<QQuickItem *>("noticeLabel");
        for (const QString &format : {QString("pdf"), QString("svg")}) {
            auto *button = visualChild(window->contentItem(), "export-" + format);
            QVERIFY(button);
            QVERIFY(button->mapToScene({0, 0}).x() > boardButton->mapToScene({0, 0}).x() + boardButton->width());
            QCOMPARE(button->mapToScene({0, 0}).y(), boardButton->mapToScene({0, 0}).y());
            QTest::mouseClick(window, Qt::LeftButton, {}, button->mapToScene({10, 17}).toPoint());
            const QString saved = dir.path() + "/downloads/Board 1." + format;
            QVERIFY(QFile::exists(saved));
            QTRY_VERIFY(noticeLabel->isVisible());
            QCOMPARE(noticeLabel->property("text").toString(), "Saved to " + saved);
        }

        // The pencil on a row renames that board, without opening it and
        // without closing the menu under the name being typed.
        auto rowAction = [&](const QString &row, const QString &action) {
            QTest::mouseClick(window, Qt::LeftButton, {}, boardButton->mapToScene({20, 17}).toPoint());
            QTRY_VERIFY(boardMenu->property("opened").toBool());
            auto *content = boardMenu->property("contentItem").value<QQuickItem *>();
            QQuickItem *item = nullptr;
            QTRY_VERIFY((item = visualChild(content, row)));
            // The pencil and the cross show on the row the pointer is over.
            QTest::mouseMove(window, item->mapToScene({40, 17}).toPoint());
            QQuickItem *button = visualChild(content, action);
            QVERIFY(button);
            QTRY_VERIFY(button->isVisible());
            QTest::mouseClick(window, Qt::LeftButton, {}, button->mapToScene({13, 13}).toPoint());
        };
        rowAction("board-1", "rename-1");
        QVERIFY(boardMenu->property("opened").toBool());
        QCOMPARE(backend.currentName(), QString("Board 1"));
        QTRY_VERIFY(window->activeFocusItem() && window->activeFocusItem()->inherits("QQuickTextInput"));
        QTest::keySequence(window, QKeySequence::SelectAll);
        type("ideas");
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(backend.boards().at(1).toMap().value("name").toString(), QString("ideas"));
        QCOMPARE(backend.currentName(), QString("Board 1"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!boardMenu->property("visible").toBool());

        // The cross asks before deleting, and doesn't open the board either.
        auto *confirm = window->findChild<QObject *>("confirm");
        rowAction("board-2", "delete-2");
        QTRY_VERIFY(confirm->property("opened").toBool());
        QCOMPARE(backend.currentName(), QString("Board 1"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QCOMPARE(backend.boards().size(), 3);

        // B opens and closes it, and F2 opens it to rename the board.
        QTest::keyClick(window, Qt::Key_B);
        QTRY_VERIFY(boardMenu->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_B);
        QTRY_VERIFY(!boardMenu->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_F2);
        QTRY_VERIFY(boardMenu->property("opened").toBool());
        QTRY_VERIFY(qobject_cast<QQuickItem *>(window->activeFocusItem())
                    && window->activeFocusItem()->inherits("QQuickTextInput"));
        QTest::keySequence(window, QKeySequence::SelectAll);
        type("plans");
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(backend.currentName(), QString("plans"));
        QTRY_COMPARE(boardName->property("text").toString(), QString("plans"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!boardMenu->property("visible").toBool());

        auto *help = window->findChild<QObject *>("help");
        QTest::keyClick(window, '?');
        QTRY_VERIFY(help->property("opened").toBool());
        // A Material popup animates out: `opened` drops at once, `visible` after.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!help->property("visible").toBool());

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCoreApplication::removeTranslator(&translator);
    }

};

QTEST_MAIN(SlateTests)
#include "slate_tests.moc"
