import zlib from "node:zlib";

const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n += 1) {
    let c = n;
    for (let k = 0; k < 8; k += 1) {
      c = (c & 1) !== 0 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buffer) {
  let crc = 0xffffffff;
  for (let i = 0; i < buffer.length; i += 1) {
    crc = CRC_TABLE[(crc ^ buffer[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function makeChunk(type, data) {
  const typeBytes = Buffer.from(type, "ascii");
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);

  const crcInput = Buffer.concat([typeBytes, data]);
  const crcBuf = Buffer.alloc(4);
  crcBuf.writeUInt32BE(crc32(crcInput), 0);

  return Buffer.concat([len, typeBytes, data, crcBuf]);
}

/**
 * Encode raw RGBA8888 pixels (row-major, top-to-bottom) into a PNG Buffer.
 * @param {Buffer|Uint8Array} rgba
 * @param {number} width
 * @param {number} height
 * @returns {Buffer}
 */
export function encodeRgbaToPng(rgba, width, height) {
  const w = Number(width);
  const h = Number(height);
  if (!Number.isInteger(w) || w <= 0 || !Number.isInteger(h) || h <= 0) {
    throw new Error(`Invalid PNG dimensions: ${width}x${height}`);
  }
  const src = Buffer.isBuffer(rgba) ? rgba : Buffer.from(rgba);
  const expected = w * h * 4;
  if (src.length !== expected) {
    throw new Error(`RGBA buffer length mismatch: got ${src.length}, expected ${expected}`);
  }

  const stride = w * 4;
  const raw = Buffer.alloc((stride + 1) * h);
  for (let y = 0; y < h; y += 1) {
    const dstOffset = y * (stride + 1);
    raw[dstOffset] = 0; // Filter type 0 (None)
    src.copy(raw, dstOffset + 1, y * stride, y * stride + stride);
  }

  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; // Bit depth
  ihdr[9] = 6; // Color type: RGBA
  ihdr[10] = 0; // Compression
  ihdr[11] = 0; // Filter
  ihdr[12] = 0; // Interlace

  const idat = zlib.deflateSync(raw);
  return Buffer.concat([
    PNG_SIGNATURE,
    makeChunk("IHDR", ihdr),
    makeChunk("IDAT", idat),
    makeChunk("IEND", Buffer.alloc(0)),
  ]);
}
