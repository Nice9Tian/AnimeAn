// MD5，只用来判断内容是否相同，不涉及安全用途。输入按 UTF-8 编码，输出 32 位十六进制。

function toUtf8(text: string): number[] {
  const bytes: number[] = [];
  for (let i = 0; i < text.length; i++) {
    let code = text.charCodeAt(i);
    if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length) {
      code = 0x10000 + ((code - 0xd800) << 10) + (text.charCodeAt(++i) - 0xdc00);
    }
    if (code < 0x80) bytes.push(code);
    else if (code < 0x800) bytes.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
    else if (code < 0x10000) bytes.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    else bytes.push(0xf0 | (code >> 18), 0x80 | ((code >> 12) & 0x3f), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
  }
  return bytes;
}

const SHIFTS = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
const SINES: number[] = [];
for (let i = 0; i < 64; i++) SINES.push(Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296) >>> 0);

function add(a: number, b: number): number {
  return (a + b) >>> 0;
}

function rotate(value: number, bits: number): number {
  return ((value << bits) | (value >>> (32 - bits))) >>> 0;
}

export function md5(text: string): string {
  const bytes = toUtf8(text);
  const bitLength = bytes.length * 8;
  bytes.push(0x80);
  while (bytes.length % 64 !== 56) bytes.push(0);
  for (let i = 0; i < 4; i++) bytes.push((bitLength >>> (8 * i)) & 0xff);
  // 长度的高 32 位
  const high = Math.floor(bitLength / 4294967296);
  for (let i = 0; i < 4; i++) bytes.push((high >>> (8 * i)) & 0xff);

  let a0 = 0x67452301;
  let b0 = 0xefcdab89;
  let c0 = 0x98badcfe;
  let d0 = 0x10325476;

  for (let offset = 0; offset < bytes.length; offset += 64) {
    const words: number[] = [];
    for (let i = 0; i < 16; i++) {
      const p = offset + i * 4;
      words.push((bytes[p] | (bytes[p + 1] << 8) | (bytes[p + 2] << 16) | (bytes[p + 3] << 24)) >>> 0);
    }
    let a = a0;
    let b = b0;
    let c = c0;
    let d = d0;
    for (let i = 0; i < 64; i++) {
      let f: number;
      let g: number;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const sum = add(add(a, f >>> 0), add(SINES[i], words[g]));
      a = d;
      d = c;
      c = b;
      b = add(b, rotate(sum, SHIFTS[Math.floor(i / 16) * 4 + (i % 4)]));
    }
    a0 = add(a0, a);
    b0 = add(b0, b);
    c0 = add(c0, c);
    d0 = add(d0, d);
  }

  let hex = "";
  for (const word of [a0, b0, c0, d0]) {
    for (let i = 0; i < 4; i++) hex += ((word >>> (8 * i)) & 0xff).toString(16).padStart(2, "0");
  }
  return hex;
}
