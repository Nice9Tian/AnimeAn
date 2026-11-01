#ifndef CLIPREADER_H
#define CLIPREADER_H

#include "algorithm/core/color.h"
#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/string.h"


// Minimal, dependency-free reader for Clip Studio Paint ".clip" documents.
//
// A .clip file is a "CSFCHUNK" container holding:
//   - CHNKHead  : file header
//   - CHNKExta  : external data blocks (raster mipmap tiles AND vector data)
//   - CHNKSQLi  : an embedded SQLite database with the layer/canvas structure
//   - CHNKFoot  : footer
//
// The hand-drawn vector strokes live in one of the CHNKExta blocks as a
// self-describing binary object list: each object is a SEQUENCE of strokes,
// every stroke being a header (header_size at +0, per-point record_size at
// +8, point_count at +16) followed by point_count records. This reader
// locates that block without touching the SQLite database (so it needs no
// SQLite dependency): a vector block is the only external block whose stroke
// chain tiles the payload exactly (sum of header_size + point_count *
// record_size == payload_size), which the raster "BlockData" tiles never
// satisfy.
//
// Only the vector layer is decoded here; the raster background (plain paper)
// is intentionally ignored.
namespace ClipReader {

struct ClipStroke {
    AnimeCore::Vector<AnimeCore::PointF> points;
    AnimeCore::Color color = AnimeCore::Color(0, 0, 0);
    double width = 3.0;
};

struct ClipDocument {
    AnimeCore::Vector<ClipStroke> strokes;
    bool valid = false;
    AnimeCore::String error;
};

// Parses the file at absolutePath. On failure returns a document with
// valid == false and a human-readable error string.
ClipDocument readClipFile(const AnimeCore::String &absolutePath);

} // namespace ClipReader

#endif // CLIPREADER_H
