import { readFileSync } from 'node:fs';

export function inspectPe(path) {
  const bytes = readFileSync(path);
  if (bytes.length < 64 || bytes.toString('ascii', 0, 2) !== 'MZ') throw new Error(`Not a PE: ${path}`);
  const pe = bytes.readUInt32LE(60);
  if (pe + 24 > bytes.length || bytes.toString('ascii', pe, pe + 4) !== 'PE\0\0') throw new Error('Invalid PE header');
  const optional = pe + 24;
  const magic = bytes.readUInt16LE(optional);
  if (![0x10b, 0x20b].includes(magic)) throw new Error('Unsupported PE optional header');
  const directoryBase = optional + (magic === 0x20b ? 112 : 96);
  const sectionBase = optional + bytes.readUInt16LE(pe + 20);
  const headerSize = bytes.readUInt32LE(optional + 60);
  const imageBase = magic === 0x20b ? bytes.readBigUInt64LE(optional + 24) : BigInt(bytes.readUInt32LE(optional + 28));
  const sections = [];
  for (let index = 0; index < bytes.readUInt16LE(pe + 6); index++) {
    const cursor = sectionBase + index * 40;
    if (cursor + 40 > bytes.length) throw new Error('Truncated section table');
    sections.push({ name: bytes.toString('ascii', cursor, cursor + 8).replaceAll('\0', ''), virtualSize: bytes.readUInt32LE(cursor + 8), rva: bytes.readUInt32LE(cursor + 12), rawSize: bytes.readUInt32LE(cursor + 16), raw: bytes.readUInt32LE(cursor + 20) });
  }
  function offset(rva, count = 1) {
    if (rva < headerSize && rva + count <= bytes.length) return rva;
    const section = sections.find(item => rva >= item.rva && rva + count <= item.rva + item.rawSize);
    if (!section) throw new Error(`RVA not backed by source bytes: ${rva.toString(16)}`);
    const result = section.raw + rva - section.rva;
    if (result + count > bytes.length) throw new Error('PE range outside source');
    return result;
  }
  function textAt(rva) {
    const start = offset(rva);
    const end = bytes.indexOf(0, start);
    if (end < start || end - start > 4096) throw new Error('Invalid imported library name');
    return bytes.toString('utf8', start, end);
  }
  const imports = new Set();
  const importRva = bytes.readUInt32LE(directoryBase + 8);
  if (importRva) {
    for (let index = 0; index < 65536; index++) {
      const cursor = offset(importRva + index * 20, 20);
      const nameRva = bytes.readUInt32LE(cursor + 12);
      if (!nameRva && bytes.subarray(cursor, cursor + 20).every(value => value === 0)) break;
      if (nameRva) imports.add(textAt(nameRva));
      if (index === 65535) throw new Error('Unterminated import directory');
    }
  }
  const delayRva = bytes.readUInt32LE(directoryBase + 13 * 8);
  if (delayRva) {
    for (let index = 0; index < 65536; index++) {
      const cursor = offset(delayRva + index * 32, 32);
      const address = bytes.readUInt32LE(cursor + 4);
      if (!address && bytes.subarray(cursor, cursor + 32).every(value => value === 0)) break;
      const rva = bytes.readUInt32LE(cursor) & 1 ? address : Number(BigInt(address) - imageBase);
      if (address) imports.add(textAt(rva));
      if (index === 65535) throw new Error('Unterminated delayed-import directory');
    }
  }
  return { machine: bytes.readUInt16LE(pe + 4), imageBase: `0x${imageBase.toString(16)}`, entrypointRva: bytes.readUInt32LE(optional + 16), sections, imports: [...imports].sort() };
}
