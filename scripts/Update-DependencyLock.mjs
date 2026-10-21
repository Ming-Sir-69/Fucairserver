import { readFileSync, readdirSync, writeFileSync, existsSync } from 'node:fs';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const msys = resolve(process.env.FUCAIRSERVER_MSYS_ROOT ?? join(root, 'tools/msys64'));
const database = join(msys, 'var/lib/pacman/local');
const cache = join(msys, 'var/cache/pacman/pkg');
const cacheFiles = readdirSync(cache);
const section = (text, name) => text.match(new RegExp(`%${name}%\\r?\\n([^\\r\\n]+)`))?.[1];
const packages = readdirSync(database).filter(name => name.startsWith('mingw-w64-ucrt-x86_64-')).map(directory => {
  const desc = readFileSync(join(database, directory, 'desc'), 'utf8');
  const name = section(desc, 'NAME'), version = section(desc, 'VERSION'), base = section(desc, 'BASE');
  const filename = cacheFiles.find(file => file === `${name}-${version}-any.pkg.tar.zst` || file === `${name}-${version}-any.pkg.tar.xz`);
  if (!filename) throw new Error(`Cached package required to lock ${name} ${version}`);
  return { name, version, base, filename, sha256: createHash('sha256').update(readFileSync(join(cache, filename))).digest('hex'), url: `https://repo.msys2.org/mingw/ucrt64/${filename}` };
}).sort((a, b) => a.name.localeCompare(b.name));
writeFileSync(join(root, 'profiles/toolchain-lock.json'), JSON.stringify({ environment: 'MSYS2 UCRT64 x86_64', packages }, null, 2) + '\n');
const runtime = JSON.parse(readFileSync(join(root, 'dist/Fucairserver/licenses/runtime-packages.json'), 'utf8'));
const indexPath = join(root, 'build/downloads/msys2-source-index.html');
if (!existsSync(indexPath)) throw new Error('Download the official MSYS2 source index before updating the source lock');
const sourceIndex = readFileSync(indexPath, 'utf8');
const sources = new Map();
for (const item of runtime) {
  const pkg = packages.find(pkg => pkg.name === item.name && pkg.version === item.version);
  if (!pkg) throw new Error(`Runtime and build lock disagree: ${item.name}`);
  const filename = ['zst', 'xz', 'gz'].map(extension => `${pkg.base}-${pkg.version}.src.tar.${extension}`).find(name => sourceIndex.includes(`"${name}"`));
  if (!filename) throw new Error(`Exact corresponding source is unavailable: ${pkg.base} ${pkg.version}`);
  sources.set(filename, { base: pkg.base, version: pkg.version, filename, url: `https://repo.msys2.org/mingw/sources/${filename}` });
}
writeFileSync(join(root, 'build/runtime-source-candidates.json'), JSON.stringify({ sources: [...sources.values()].sort((a,b) => a.filename.localeCompare(b.filename)) }, null, 2) + '\n');
console.log(JSON.stringify({ locked_packages: packages.length, corresponding_sources: sources.size }));
