#include "clipreader.h"


#include <cmath>
#include <cstring>

#include "algorithm/core/containers.h"
#include "algorithm/core/geometry.h"
#include "algorithm/core/string.h"

#include <cstdint>

using namespace AnimeCore;

namespace {

// Sanity caps to keep a malformed or hostile file from forcing huge work.
constexpr std::int64_t kMaxFileSize = 512LL * 1024 * 1024; // 512 MiB
constexpr std::uint32_t kMaxRecordSize = 1u << 16;         // 64 KiB per point record
constexpr std::uint32_t kMaxPointsPerObject = 5000000u;
constexpr std::uint32_t kMaxStrokesPerBlock = 100000u;
constexpr std::uint32_t kMinHeaderBody = 28u;              // must cover the header ints we read
// Coordinates far outside any plausible canvas are treated as corruption:
// letting them through would overflow the import path's bounds arithmetic.
constexpr double kMaxCoordinate = 1e12;

// Bounds-checked big-endian scalar reads over a raw byte span. Every read
// verifies the requested window lies fully inside [0, size) before touching
// memory, so a truncated or crafted file can never read out of bounds.
class Cursor {
public:
    Cursor(const char *data, std::int64_t size)
        : m_data(data), m_size(size) {}

    std::int64_t size() const { return m_size; }

    bool has(std::int64_t offset, std::int64_t count) const {
        return offset >= 0 && count >= 0 && offset <= m_size && count <= m_size - offset;
    }

    bool matches(std::int64_t offset, const char *literal, std::int64_t length) const {
        if (!has(offset, length)) {
            return false;
        }
        return std::memcmp(m_data + offset, literal, static_cast<size_t>(length)) == 0;
    }

    bool u32(std::int64_t offset, std::uint32_t *out) const {
        if (!has(offset, 4)) {
            return false;
        }
        *out = qFromBigEndian<std::uint32_t>(reinterpret_cast<const unsigned char *>(m_data + offset));
        return true;
    }

    bool u64(std::int64_t offset, std::uint64_t *out) const {
        if (!has(offset, 8)) {
            return false;
        }
        *out = qFromBigEndian<std::uint64_t>(reinterpret_cast<const unsigned char *>(m_data + offset));
        return true;
    }

    bool f64(std::int64_t offset, double *out) const {
        std::uint64_t bits = 0;
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
    std::int64_t m_size = 0;
};

// Attempts to decode [offset, offset+length) of the file as a Clip vector
// object block. An object (u64 size + payload) holds a SEQUENCE of strokes,
// each being a self-describing header (its size at +0, the per-point record
// size at +8, the point count at +16) followed by pointCount records; single-
// stroke files are simply the one-element case. Returns true and fills
// strokes only if every stroke validates and the block is consumed exactly,
// so raster "BlockData" tiles are never misread as vectors.
bool decodeVectorBlock(const Cursor &cur, std::int64_t offset, std::int64_t length,
                       Vector<ClipReader::ClipStroke> *strokes)
{
    if (length < 8) {
        return false;
    }

    Vector<ClipReader::ClipStroke> decoded;
    std::int64_t pos = offset;
    const std::int64_t end = offset + length;
    std::uint32_t strokeGuard = 0;

    while (pos + 8 <= end) {
        std::uint64_t total = 0;
        if (!cur.u64(pos, &total)) {
            return false;
        }
        // Object payload is the `total` bytes following the 8-byte size field.
        if (total < kMinHeaderBody || total > static_cast<std::uint64_t>(end - (pos + 8))) {
            return false;
        }

        const std::int64_t objectBody = pos + 8;
        const std::int64_t objectEnd = objectBody + static_cast<std::int64_t>(total);

        std::int64_t strokePos = objectBody;
        while (strokePos + kMinHeaderBody <= objectEnd) {
            std::uint32_t headerBody = 0;
            std::uint32_t recordSize = 0;
            std::uint32_t pointCount = 0;
            if (!cur.u32(strokePos + 0, &headerBody) ||
                !cur.u32(strokePos + 8, &recordSize) ||
                !cur.u32(strokePos + 16, &pointCount)) {
                return false;
            }

            if (headerBody < kMinHeaderBody ||
                static_cast<std::uint64_t>(headerBody) > static_cast<std::uint64_t>(objectEnd - strokePos)) {
                return false;
            }
            if (recordSize <= 16 || recordSize > kMaxRecordSize) {
                return false;
            }
            if (pointCount == 0 || pointCount > kMaxPointsPerObject) {
                return false;
            }
            const std::uint64_t pointBytes = static_cast<std::uint64_t>(recordSize) * pointCount;
            if (pointBytes > static_cast<std::uint64_t>(objectEnd - strokePos - headerBody)) {
                return false;
            }

            const std::int64_t pointsStart = strokePos + headerBody;
            ClipReader::ClipStroke stroke;
            stroke.points.reserve(static_cast<qsizetype>(pointCount));
            for (std::uint32_t i = 0; i < pointCount; ++i) {
                const std::int64_t rec = pointsStart + static_cast<std::int64_t>(std::uint64_t(i) * recordSize);
                double x = 0.0;
                double y = 0.0;
                if (!cur.f64(rec + 0, &x) || !cur.f64(rec + 8, &y)) {
                    return false;
                }
                if (!std::isfinite(x) || !std::isfinite(y) ||
                    std::abs(x) > kMaxCoordinate || std::abs(y) > kMaxCoordinate) {
                    return false;
                }
                stroke.points.append(PointF(x, y));
            }
            if (stroke.points.size() >= 2) {
                decoded.append(stroke);
            }

            strokePos = pointsStart + static_cast<std::int64_t>(pointBytes);
            if (++strokeGuard > kMaxStrokesPerBlock) {
                return false;
            }
        }

        // Every stroke in the object must line up exactly with its end.
        if (strokePos != objectEnd) {
            return false;
        }
        pos = objectEnd;
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

ClipDocument readClipFile(const String &absolutePath)
{
    ClipDocument document;

    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        document.error = ANIME_STR("Cannot open file: %1").arg(file.errorString());
        return document;
    }
    if (file.size() <= 0 || file.size() > kMaxFileSize) {
        document.error = ANIME_STR("Unsupported file size.");
        return document;
    }

    const QByteArray bytes = file.readAll();
    const Cursor cur(bytes.constData(), bytes.size());

    if (!cur.matches(0, "CSFCHUNK", 8)) {
        document.error = ANIME_STR("Not a Clip Studio Paint file (missing CSFCHUNK header).");
        return document;
    }

    // Walk the chunk stream. Each chunk is an 8-byte tag + 8-byte big-endian
    // size + payload. We resync by scanning for known tags so a single odd
    // chunk cannot derail the walk.
    std::int64_t pos = 8;
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

        std::uint64_t chunkSize = 0;
        if (!cur.u64(pos + 8, &chunkSize)) {
            break;
        }
        const std::int64_t payload = pos + 16;
        if (!cur.has(payload, static_cast<std::int64_t>(chunkSize))) {
            break; // truncated chunk; stop cleanly with whatever we found
        }

        if (isExternal) {
            // Payload: 8-byte id length + "extrnlid"+id + block data.
            std::uint64_t idLen = 0;
            if (cur.u64(payload, &idLen) && idLen <= chunkSize && chunkSize - idLen >= 8) {
                const std::int64_t dataStart = payload + 8 + static_cast<std::int64_t>(idLen);
                const std::int64_t dataLen = static_cast<std::int64_t>(chunkSize) - 8 - static_cast<std::int64_t>(idLen);
                decodeVectorBlock(cur, dataStart, dataLen, &document.strokes);
            }
        }

        pos = payload + static_cast<std::int64_t>(chunkSize);
    }

    if (document.strokes.isEmpty()) {
        document.error = ANIME_STR("No vector strokes were found in this .clip file.");
        return document;
    }

    document.valid = true;
    return document;
}

} // namespace ClipReader
