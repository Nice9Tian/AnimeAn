#include "clipreader.h"

#include <QFile>
#include <QtEndian>
#include <QtGlobal>

#include <cmath>
#include <cstring>

namespace {

// Sanity caps to keep a malformed or hostile file from forcing huge work.
constexpr qint64 kMaxFileSize = 512LL * 1024 * 1024; // 512 MiB
constexpr quint32 kMaxRecordSize = 1u << 16;         // 64 KiB per point record
constexpr quint32 kMaxPointsPerObject = 5000000u;
constexpr quint32 kMaxObjectsPerBlock = 100000u;
constexpr quint32 kMinHeaderBody = 28u;              // must cover the header ints we read

// Bounds-checked big-endian scalar reads over a raw byte span. Every read
// verifies the requested window lies fully inside [0, size) before touching
// memory, so a truncated or crafted file can never read out of bounds.
class Cursor {
public:
    Cursor(const char *data, qint64 size)
        : m_data(data), m_size(size) {}

    qint64 size() const { return m_size; }

    bool has(qint64 offset, qint64 count) const {
        return offset >= 0 && count >= 0 && offset <= m_size && count <= m_size - offset;
    }

    bool matches(qint64 offset, const char *literal, qint64 length) const {
        if (!has(offset, length)) {
            return false;
        }
        return std::memcmp(m_data + offset, literal, static_cast<size_t>(length)) == 0;
    }

    bool u32(qint64 offset, quint32 *out) const {
        if (!has(offset, 4)) {
            return false;
        }
        *out = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(m_data + offset));
        return true;
    }

    bool u64(qint64 offset, quint64 *out) const {
        if (!has(offset, 8)) {
            return false;
        }
        *out = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(m_data + offset));
        return true;
    }

    bool f64(qint64 offset, double *out) const {
        quint64 bits = 0;
        if (!u64(offset, &bits)) {
            return false;
        }
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        *out = value;
        return true;
    }

private:
    const char *m_data = nullptr;
    qint64 m_size = 0;
};

// Attempts to decode [offset, offset+length) of the file as a Clip vector
// object block (one or more self-describing point-list objects). Returns true
// and fills strokes only if every object validates; a single inconsistency
// makes this reject the block, so raster tiles are never misread as vectors.
bool decodeVectorBlock(const Cursor &cur, qint64 offset, qint64 length,
                       QVector<ClipReader::ClipStroke> *strokes)
{
    if (length < 8) {
        return false;
    }

    QVector<ClipReader::ClipStroke> decoded;
    qint64 pos = offset;
    const qint64 end = offset + length;
    quint32 objectCount = 0;

    while (pos + 8 <= end) {
        quint64 total = 0;
        if (!cur.u64(pos, &total)) {
            return false;
        }
        // Object payload is the `total` bytes following the 8-byte size field.
        if (total < kMinHeaderBody || total > static_cast<quint64>(end - (pos + 8))) {
            return false;
        }

        const qint64 body = pos + 8;
        quint32 headerBody = 0;
        quint32 recordSize = 0;
        quint32 pointCount = 0;
        if (!cur.u32(body + 0, &headerBody) ||
            !cur.u32(body + 8, &recordSize) ||
            !cur.u32(body + 16, &pointCount)) {
            return false;
        }

        if (headerBody < kMinHeaderBody || static_cast<quint64>(headerBody) >= total) {
            return false;
        }
        if (recordSize <= 16 || recordSize > kMaxRecordSize) {
            return false;
        }

        const quint64 pointBytes = total - headerBody;
        if (pointBytes % recordSize != 0) {
            return false;
        }
        const quint64 count = pointBytes / recordSize;
        if (count == 0 || count > kMaxPointsPerObject) {
            return false;
        }
        // The stored point count must agree with the size-derived count
        // (allow a tiny slack for trailing padding records).
        const quint64 diff = count > pointCount ? count - pointCount : pointCount - count;
        if (diff > 2) {
            return false;
        }

        const qint64 pointsStart = body + headerBody;
        ClipReader::ClipStroke stroke;
        stroke.points.reserve(static_cast<int>(qMin<quint64>(count, kMaxPointsPerObject)));
        bool coordsValid = true;
        for (quint64 i = 0; i < count; ++i) {
            const qint64 rec = pointsStart + static_cast<qint64>(i * recordSize);
            double x = 0.0;
            double y = 0.0;
            if (!cur.f64(rec + 0, &x) || !cur.f64(rec + 8, &y)) {
                return false;
            }
            if (!std::isfinite(x) || !std::isfinite(y)) {
                coordsValid = false;
                break;
            }
            stroke.points.append(QPointF(x, y));
        }
        if (!coordsValid) {
            return false;
        }
        if (stroke.points.size() >= 2) {
            decoded.append(stroke);
        }

        pos = body + static_cast<qint64>(total);
        if (++objectCount > kMaxObjectsPerBlock) {
            return false;
        }
    }

    // The block must be consumed exactly, and must contain at least one usable
    // stroke, before we accept it as vector data.
    if (pos != end || decoded.isEmpty()) {
        return false;
    }

    *strokes += decoded;
    return true;
}

} // namespace

namespace ClipReader {

ClipDocument readClipFile(const QString &absolutePath)
{
    ClipDocument document;

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        document.error = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return document;
    }
    if (file.size() <= 0 || file.size() > kMaxFileSize) {
        document.error = QStringLiteral("Unsupported file size.");
        return document;
    }

    const QByteArray bytes = file.readAll();
    const Cursor cur(bytes.constData(), bytes.size());

    if (!cur.matches(0, "CSFCHUNK", 8)) {
        document.error = QStringLiteral("Not a Clip Studio Paint file (missing CSFCHUNK header).");
        return document;
    }

    // Walk the chunk stream. Each chunk is an 8-byte tag + 8-byte big-endian
    // size + payload. We resync by scanning for known tags so a single odd
    // chunk cannot derail the walk.
    qint64 pos = 8;
    int guard = 0;
    while (pos + 16 <= cur.size()) {
        if (++guard > 1000000) {
            break;
        }

        const bool isExternal = cur.matches(pos, "CHNKExta", 8);
        const bool known = isExternal || cur.matches(pos, "CHNKHead", 8) ||
                           cur.matches(pos, "CHNKSQLi", 8) || cur.matches(pos, "CHNKFoot", 8);
        if (!known) {
            ++pos;
            continue;
        }

        quint64 chunkSize = 0;
        if (!cur.u64(pos + 8, &chunkSize)) {
            break;
        }
        const qint64 payload = pos + 16;
        if (!cur.has(payload, static_cast<qint64>(chunkSize))) {
            break; // truncated chunk; stop cleanly with whatever we found
        }

        if (isExternal) {
            // Payload: 8-byte id length + "extrnlid"+id + block data.
            quint64 idLen = 0;
            if (cur.u64(payload, &idLen) && idLen <= chunkSize && chunkSize - idLen >= 8) {
                const qint64 dataStart = payload + 8 + static_cast<qint64>(idLen);
                const qint64 dataLen = static_cast<qint64>(chunkSize) - 8 - static_cast<qint64>(idLen);
                decodeVectorBlock(cur, dataStart, dataLen, &document.strokes);
            }
        }

        pos = payload + static_cast<qint64>(chunkSize);
    }

    if (document.strokes.isEmpty()) {
        document.error = QStringLiteral("No vector strokes were found in this .clip file.");
        return document;
    }

    document.valid = true;
    return document;
}

} // namespace ClipReader
