#pragma once

// The drawn outline of a shape or connector, built once per change instead of
// on every paint: each pen event repaints the page, objects included.

#include "../layers/VectorLayer.h"

#include <QPainter>

template <typename Key>
struct OutlineCache {
    Key key{};
    bool valid = false;
    VectorStroke stroke;
    StrokePolygonResult poly;

    /// The outline @p build() makes, built again only when @p newKey changed.
    template <typename Build>
    const VectorStroke& get(const Key& newKey, Build build)
    {
        if (!valid || !(newKey == key)) {
            key = newKey;
            stroke = build();
            poly = VectorLayer::buildStrokePolygon(stroke);
            valid = true;
        }
        return stroke;
    }

    /// Draw the outline @p build() makes, building it again only when @p newKey changed.
    template <typename Build>
    void render(QPainter& painter, const Key& newKey, Build build)
    {
        get(newKey, build);
        // The paint event clips to the repainted area: skip an outline that misses it.
        if (painter.hasClipping()
            && !painter.clipBoundingRect().intersects(stroke.boundingBox.adjusted(-2, -2, 2, 2))) {
            return;
        }
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        VectorLayer::renderStroke(painter, stroke, poly);
        painter.restore();
    }
};
