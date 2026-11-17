#include "dotgrid.h"

#include <QQmlEngine>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <cmath>

DotGrid::DotGrid(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
}

void DotGrid::registerType() {
    qmlRegisterType<DotGrid>("Slate", 1, 0, "DotGrid");
}

void DotGrid::setOffsetX(qreal value) {
    if (value == m_offsetX)
        return;
    m_offsetX = value;
    emit changed();
    update();
}

void DotGrid::setOffsetY(qreal value) {
    if (value == m_offsetY)
        return;
    m_offsetY = value;
    emit changed();
    update();
}

void DotGrid::setStep(qreal value) {
    if (value == m_step)
        return;
    m_step = value;
    m_rebuild = true;
    emit changed();
    update();
}

void DotGrid::setDotSize(qreal value) {
    if (value == m_dotSize)
        return;
    m_dotSize = value;
    m_rebuild = true;
    emit changed();
    update();
}

void DotGrid::setColor(const QColor &value) {
    if (value == m_color)
        return;
    m_color = value;
    m_recolor = true;
    emit changed();
    update();
}

void DotGrid::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        m_rebuild = true;
        update();
    }
}

QSGNode *DotGrid::updatePaintNode(QSGNode *node, UpdatePaintNodeData *) {
    auto *root = static_cast<QSGTransformNode *>(node);
    QSGGeometryNode *dots = nullptr;
    if (!root) {
        root = new QSGTransformNode;
        dots = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        dots->setGeometry(geometry);
        dots->setFlag(QSGNode::OwnsGeometry);
        dots->setMaterial(new QSGFlatColorMaterial);
        dots->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(dots);
        m_rebuild = m_recolor = true;
    } else {
        dots = static_cast<QSGGeometryNode *>(root->firstChild());
    }

    if (m_rebuild) {
        m_rebuild = false;
        // One spare row and column each side, for the slide below.
        const int columns = m_step >= 4 ? int(std::ceil(width() / m_step)) + 2 : 0;
        const int rows = m_step >= 4 ? int(std::ceil(height() / m_step)) + 2 : 0;
        QSGGeometry *geometry = dots->geometry();
        geometry->allocate(columns * rows * 6);
        QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
        const float half = float(m_dotSize) / 2;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                const float x = float(c * m_step), y = float(r * m_step);
                v[0].set(x - half, y - half);
                v[1].set(x + half, y - half);
                v[2].set(x - half, y + half);
                v[3].set(x + half, y - half);
                v[4].set(x + half, y + half);
                v[5].set(x - half, y + half);
                v += 6;
            }
        }
        dots->markDirty(QSGNode::DirtyGeometry);
    }
    if (m_recolor) {
        m_recolor = false;
        static_cast<QSGFlatColorMaterial *>(dots->material())->setColor(m_color);
        dots->markDirty(QSGNode::DirtyMaterial);
    }

    auto wrapped = [this](qreal offset) {
        if (m_step < 4)
            return 0.0;
        const qreal m = std::fmod(offset, m_step);
        return (m < 0 ? m + m_step : m) - m_step;
    };
    QMatrix4x4 matrix;
    matrix.translate(float(wrapped(m_offsetX)), float(wrapped(m_offsetY)));
    root->setMatrix(matrix);
    return root;
}
