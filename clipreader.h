#ifndef CLIPREADER_H
#define CLIPREADER_H

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

// Minimal, dependency-free reader for Clip Studio Paint ".clip" documents.
//
// A .clip file is a "CSFCHUNK" container holding:
//   - CHNKHead  : file header
//   - CHNKExta  : external data blocks (raster mipmap tiles AND vector data)
//   - CHNKSQLi  : an embedded SQLite database with the layer/canvas structure
//   - CHNKFoot  : footer
//
// The hand-drawn vector strokes live in one of the CHNKExta blocks as a
// self-describing binary object list. This reader locates that block without
// touching the SQLite database (so it needs no SQLite dependency): a vector
// object block is the only external block whose header arithmetic
// (header_size + point_count * record_size == payload_size) is internally
// consistent, which the raster "BlockData" tiles never satisfy.
//
// Only the vector layer is decoded here; the raster background (plain paper)
// is intentionally ignored.
namespace ClipReader {

struct ClipStroke {
    QVector<QPointF> points;
    QColor color = Qt::black;
    qreal width = 3.0;
};

struct ClipDocument {
    QVector<ClipStroke> strokes;
    bool valid = false;
    QString error;
};

// Parses the file at absolutePath. On failure returns a document with
// valid == false and a human-readable error string.
ClipDocument readClipFile(const QString &absolutePath);

} // namespace ClipReader

#endif // CLIPREADER_H
