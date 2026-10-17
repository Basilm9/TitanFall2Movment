// Respawn VPK (v2, Titanfall) directory reader. Read-only.
// Usage: node vpkdir.js <x_dir.vpk> [regex]   -> lists matching entries with chunk info
//        node vpkdir.js <x_dir.vpk> <regex> <outDir>  -> extracts matching entries whose chunks are stored uncompressed
const fs = require('fs');
const path = require('path');

const [dirPath, reStr = '.', outDir] = process.argv.slice(2);
const re = new RegExp(reStr, 'i');
const buf = fs.readFileSync(dirPath);
if (buf.readUInt32LE(0) !== 0x55aa1234) throw new Error('not a VPK');
const version = buf.readUInt16LE(4);
const treeSize = buf.readUInt32LE(8);
let p = 16; // v2 header: sig, ver(2), minor(2), treeSize, signatureSize
const end = p + treeSize;

function str() {
  const s = p;
  while (buf[p] !== 0) p++;
  return buf.toString('latin1', s, p++);
}

const entries = [];
while (p < end) {
  const ext = str();
  if (!ext) break;
  while (true) {
    const dir = str();
    if (!dir) break;
    while (true) {
      const name = str();
      if (!name) break;
      const crc = buf.readUInt32LE(p); p += 4;
      const preload = buf.readUInt16LE(p); p += 2;
      const chunks = [];
      while (true) {
        const archive = buf.readUInt16LE(p);
        const loadFlags = buf.readUInt16LE(p + 2);
        const texFlags = buf.readUInt32LE(p + 4);
        const offset = Number(buf.readBigUInt64LE(p + 8));
        const csize = Number(buf.readBigUInt64LE(p + 16));
        const usize = Number(buf.readBigUInt64LE(p + 24));
        const term = buf.readUInt16LE(p + 32);
        p += 34;
        chunks.push({ archive, offset, csize, usize });
        if (term === 0xffff) break;
      }
      p += preload;
      entries.push({ path: `${dir === ' ' ? '' : dir + '/'}${name}.${ext}`, crc, chunks });
    }
  }
}

const hits = entries.filter(e => re.test(e.path));
if (!outDir) {
  console.log(`version ${version}, ${entries.length} entries, ${hits.length} match`);
  for (const e of hits) console.log(e.path, e.chunks.map(c => `a${c.archive}@${c.offset}:${c.csize}/${c.usize}`).join(' '));
  process.exit(0);
}

// LZHAM decompression via pg9182/tf2lzham's WASI build (tf2lzham.wasm, MIT), mirroring its Go wrapper.
const { WASI } = require('node:wasi');
const wasmModule = new WebAssembly.Module(fs.readFileSync(path.join(__dirname, 'vendor', 'tf2lzham.wasm')));

function lzhamDecompress(src, dstLen) {
  const wasi = new WASI({ version: 'preview1' });
  const inst = new WebAssembly.Instance(wasmModule, wasi.getImportObject());
  const { tf2lzham_malloc: malloc, tf2lzham_decompress: decompress, tf2lzham_decompress_strerror: strerror, memory } = inst.exports;
  const ptr = malloc(8 + 8 + 8 + dstLen + src.length);
  const lenOff = ptr, adlOff = ptr + 8, crcOff = ptr + 16, dstOff = ptr + 24, srcOff = dstOff + dstLen;
  let dv = new DataView(memory.buffer);
  dv.setUint32(lenOff, dstLen, true);
  dv.setUint32(adlOff, 0, true);
  dv.setUint32(crcOff, 0, true);
  new Uint8Array(memory.buffer).set(src, srcOff);
  const status = decompress(dstOff, lenOff, srcOff, src.length, adlOff, crcOff);
  const errPtr = strerror(status);
  if (errPtr !== 0) {
    const m = new Uint8Array(memory.buffer, errPtr, 128);
    throw new Error('lzham: ' + Buffer.from(m.subarray(0, m.indexOf(0))).toString());
  }
  dv = new DataView(memory.buffer);
  const n = dv.getUint32(lenOff, true);
  return Buffer.from(new Uint8Array(memory.buffer, dstOff, n));
}

// archive file name: <prefix>_<archive>.vpk where dir file is englishclient_X.pak000_dir.vpk -> client_X.pak000_NNN.vpk
const base = path.basename(dirPath).replace(/^english/, '').replace('_dir.vpk', '');
let ok = 0, failed = 0;
for (const e of hits) {
  try {
    const parts = [];
    for (const c of e.chunks) {
      const arc = path.join(path.dirname(dirPath), `${base}_${String(c.archive).padStart(3, '0')}.vpk`);
      const fd = fs.openSync(arc, 'r');
      const b = Buffer.alloc(c.csize);
      fs.readSync(fd, b, 0, c.csize, c.offset);
      fs.closeSync(fd);
      parts.push(c.csize === c.usize ? b : lzhamDecompress(b, c.usize));
    }
    const data = Buffer.concat(parts);
    if ((require('node:zlib').crc32(data) >>> 0) !== e.crc) throw new Error('crc mismatch');
    const dest = path.join(outDir, e.path);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, data);
    ok++;
  } catch (err) {
    failed++;
    console.log('failed:', e.path, err.message);
  }
}
console.log(`extracted ${ok}, failed ${failed}`);
