#include "CoverPainter.h"
#include "Document.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>

namespace CoverPainter {

namespace {

const char* const kDesigns[] = {"plain", "spine", "stripes", "dots", "frame",
                                "label", "linen", "split", "picture"};

QPixmap picture(const QString& path)
{
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile()) return {};
    // Keyed by the file's time too: a notebook's new picture keeps the same name.
    const QString key = QStringLiteral("cover:%1:").arg(info.lastModified().toMSecsSinceEpoch()) + path;
    QPixmap pm;
    if (QPixmapCache::find(key, &pm)) return pm;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid() && qMax(size.width(), size.height()) > 600)
        reader.setScaledSize(size.scaled(600, 600, Qt::KeepAspectRatio));
    pm = QPixmap::fromImage(reader.read());
    if (!pm.isNull()) QPixmapCache::insert(key, pm);
    return pm;
}

// The title, as large as fits the box (word wrap, then smaller).
void drawTitle(QPainter& p, const QRectF& box, const QString& title, const QColor& ink)
{
    QFont font = p.font();
    font.setBold(true);
    const int flags = Qt::AlignCenter | Qt::TextWordWrap;
    for (qreal px = box.width() * 0.16; px >= 5; px *= 0.85) {
        font.setPixelSize(qMax(5, qRound(px)));
        if (QFontMetricsF(font).boundingRect(box, flags, title).height() <= box.height()) break;
    }
    p.save();
    p.setClipRect(box, Qt::IntersectClip);
    p.setFont(font);
    p.setPen(ink);
    p.drawText(box, flags, title);
    p.restore();
}

} // namespace

QStringList designs()
{
    QStringList ids;
    for (const char* id : kDesigns) ids << QString::fromLatin1(id);
    return ids;
}

QString displayName(const QString& design)
{
    static const char* const names[] = {
        QT_TRANSLATE_NOOP("CoverPainter", "Plain"),  QT_TRANSLATE_NOOP("CoverPainter", "Spine"),
        QT_TRANSLATE_NOOP("CoverPainter", "Stripes"), QT_TRANSLATE_NOOP("CoverPainter", "Dots"),
        QT_TRANSLATE_NOOP("CoverPainter", "Frame"),  QT_TRANSLATE_NOOP("CoverPainter", "Label"),
        QT_TRANSLATE_NOOP("CoverPainter", "Linen"),  QT_TRANSLATE_NOOP("CoverPainter", "Split"),
        QT_TRANSLATE_NOOP("CoverPainter", "Picture"),
    };
    const int i = int(designs().indexOf(design));
    return i < 0 ? design : QCoreApplication::translate("CoverPainter", names[i]);
}

QColor color(const QJsonObject& cover)
{
    const QColor c(cover.value(QStringLiteral("color")).toString());
    return c.isValid() ? c : QColor(0x4a, 0x6f, 0xa5);
}

QString picturePath(const QJsonObject& cover, const QString& baseDir)
{
    const QString image = cover.value(QStringLiteral("image")).toString();
    if (image.isEmpty()) return {};
    return QFileInfo(image).isAbsolute() ? image : QDir(baseDir).filePath(image);
}

void paint(QPainter& p, const QRectF& r, const QJsonObject& cover,
           const QString& title, const QString& baseDir)
{
    if (r.isEmpty()) return;
    const QString design = cover.value(QStringLiteral("design")).toString(QStringLiteral("plain"));
    const QColor c = color(cover);
    const bool lightCover = c.lightness() > 150;
    const QColor tone = lightCover ? c.darker(118) : c.lighter(128);   // the pattern's colour
    QColor ink = lightCover ? QColor(0x33, 0x33, 0x33) : QColor(Qt::white);
    const qreal w = r.width(), h = r.height();
    const qreal radius = qMax(1.5, w * 0.05);
    // Below 60 px wide (list icons) a title cannot be read: left out.
    const bool showTitle = cover.value(QStringLiteral("showTitle")).toBool(true) && !title.isEmpty() && w >= 60;
    QRectF titleBox(r.left() + w * 0.12, r.top() + h * 0.16, w * 0.76, h * 0.32);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(r, radius, radius);
    p.save();   // the clip ends before the edge is drawn
    p.setClipPath(shape, Qt::IntersectClip);
    p.fillRect(r, c);
    p.setPen(Qt::NoPen);

    if (design == QLatin1String("spine")) {
        p.fillRect(QRectF(r.left(), r.top(), w * 0.14, h), c.darker(150));
        p.fillRect(QRectF(r.left() + w * 0.14, r.top(), qMax(1.0, w * 0.012), h), c.lighter(135));
        titleBox.setLeft(r.left() + w * 0.22);
        titleBox.setRight(r.right() - w * 0.08);
    } else if (design == QLatin1String("stripes")) {
        // Bands across the lower half; the title sits on the plain top.
        p.setBrush(tone);
        for (int i = 0; i < 3; ++i)
            p.drawRect(QRectF(r.left(), r.top() + h * (0.62 + i * 0.1), w, h * 0.045));
    } else if (design == QLatin1String("dots")) {
        QColor dot = tone;
        dot.setAlpha(170);
        p.setBrush(dot);
        const qreal step = w / 9;
        for (qreal y = r.top() + h * 0.56; y < r.bottom(); y += step)
            for (qreal x = r.left() + step / 2; x < r.right(); x += step)
                p.drawEllipse(QPointF(x, y), step * 0.17, step * 0.17);
    } else if (design == QLatin1String("frame")) {
        const qreal inset = w * 0.08;
        p.setPen(QPen(lightCover ? c.darker(150) : c.lighter(160), qMax(1.0, w * 0.015)));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(inset, inset, -inset, -inset), radius * 0.6, radius * 0.6);
        p.setPen(Qt::NoPen);
    } else if (design == QLatin1String("label")) {
        const QRectF label = titleBox.adjusted(-w * 0.03, 0, w * 0.03, 0);
        p.setBrush(QColor(0xfa, 0xfa, 0xf7));
        p.setPen(QPen(c.darker(lightCover ? 125 : 110), qMax(0.6, w * 0.006)));   // still seen on a pale cover
        p.drawRoundedRect(label, w * 0.03, w * 0.03);
        p.setPen(Qt::NoPen);
        titleBox = label.adjusted(w * 0.05, h * 0.02, -w * 0.05, -h * 0.02);
        ink = QColor(0x33, 0x33, 0x33);
    } else if (design == QLatin1String("linen")) {
        // A fine weave: one-pixel threads both ways, barely visible.
        p.setPen(QPen(lightCover ? QColor(0, 0, 0, 22) : QColor(255, 255, 255, 26), 0));
        const qreal step = qMax(2.0, w / 55);
        for (qreal y = r.top(); y < r.bottom(); y += step) p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        for (qreal x = r.left(); x < r.right(); x += step) p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(Qt::NoPen);
    } else if (design == QLatin1String("split")) {
        p.fillRect(QRectF(r.left(), r.top() + h * 0.64, w, h * 0.36), lightCover ? c.darker(125) : c.lighter(165));
        p.fillRect(QRectF(r.left(), r.top() + h * 0.64, w, qMax(1.0, h * 0.008)), c.darker(140));
    } else if (design == QLatin1String("picture")) {
        const QPixmap pm = picture(picturePath(cover, baseDir));
        if (!pm.isNull()) {
            const QSizeF s = QSizeF(pm.size()).scaled(r.size(), Qt::KeepAspectRatioByExpanding);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawPixmap(QRectF(r.center() - QPointF(s.width() / 2, s.height() / 2), s), pm, QRectF(pm.rect()));
        }
        // The title on a dark band at the bottom: readable on any picture.
        titleBox = QRectF(r.left(), r.bottom() - h * 0.26, w, h * 0.26);
        if (showTitle) p.fillRect(titleBox, QColor(0, 0, 0, 120));
        titleBox.adjust(w * 0.08, h * 0.02, -w * 0.08, -h * 0.02);
        ink = Qt::white;
    }

    // A thin edge, so a white cover still has an outline on a white page.
    p.restore();
    p.setPen(QPen(c.darker(lightCover ? 125 : 140), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);

    if (showTitle) drawTitle(p, titleBox, title, ink);
    p.restore();
}

QString adoptPicture(const QString& source, const QString& dir, const QString& baseName, QString* error)
{
    QImageReader reader(source);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error) *error = QCoreApplication::translate("CoverPainter", "This file is not a picture SpeedyNote can read.");
        return {};
    }
    if (qMax(image.width(), image.height()) > 1200)
        image = image.scaled(1200, 1200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    // A photo stays small as JPEG; see-through parts need PNG (also the fallback).
    bool jpeg = !image.hasAlphaChannel() && image.save(&buffer, "JPG", 88);
    if (!jpeg) {
        data.clear();
        buffer.seek(0);
        image.save(&buffer, "PNG");
    }
    const QString name = baseName + (jpeg ? QStringLiteral(".jpg") : QStringLiteral(".png"));
    if (data.isEmpty() || !QDir().mkpath(dir)) {
        if (error) *error = QCoreApplication::translate("CoverPainter", "Could not copy the picture into %1.")
                                .arg(QDir::toNativeSeparators(dir));
        return {};
    }
    if (!Document::commitFile(QDir(dir).filePath(name), data, error)) return {};
    return name;
}

} // namespace CoverPainter
