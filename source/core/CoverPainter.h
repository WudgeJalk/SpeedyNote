#pragma once

// ============================================================================
// CoverPainter - covers of notes and notebooks (Step N3)
// ============================================================================
// A cover is a small JSON object, every key optional:
//   {"design": "spine", "color": "#4a6fa5", "showTitle": true, "image": "cover.jpg"}
// Saved as the "cover" key of a note's document.json (the picture in its
// assets/images) and of a notebook's .speedynote-section.json (the picture in
// the folder, .speedynote-cover.<ext>). Older versions ignore the key, so a
// card shows page 1 there. The designs are drawn by code; "picture" fills the
// cover with the picture. "image" is a file name inside baseDir, or an
// absolute path while it is only chosen (adoptPicture copies it).
// ============================================================================

#include <QColor>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>

class QPainter;

namespace CoverPainter {

QStringList designs();                         ///< design ids, in the picker's order
QString displayName(const QString& design);
QColor color(const QJsonObject& cover);        ///< its colour, else the default blue
/// The picture file of the cover, or empty.
QString picturePath(const QJsonObject& cover, const QString& baseDir);

/// Draws cover into r. The title only when "showTitle" (default true) and r is
/// wide enough to read it. An unknown design draws as "plain". GUI thread.
void paint(QPainter& p, const QRectF& r, const QJsonObject& cover,
           const QString& title, const QString& baseDir);

/// Copies a picture into dir as baseName.jpg (or .png when it has see-through
/// parts), at most 1200 px on its long side, through Document::commitFile.
/// Returns the file name, or empty with *error.
QString adoptPicture(const QString& source, const QString& dir, const QString& baseName,
                     QString* error = nullptr);

} // namespace CoverPainter
