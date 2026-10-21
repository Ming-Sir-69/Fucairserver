import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve, join, basename, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { inspectPe } from './pe-imports.mjs';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const publicBuild = process.argv.includes('--public');
const version = readFileSync(join(root, 'VERSION'), 'utf8').trim();
const target = process.argv.slice(2).find(arg => !arg.startsWith('--'));
const output = resolve(root, target ?? (publicBuild ? `dist/packages/public-${version}-${Date.now()}` : 'dist/Fucairserver'));
if (!output.startsWith(join(root, 'dist') + sep)) throw new Error('Package output must be a directory inside this workspace dist/');
if (publicBuild && existsSync(output) && readdirSync(output).length) throw new Error('Public packages require a fresh empty output directory');
const msysRoot = resolve(process.env.FUCAIRSERVER_MSYS_ROOT ?? join(root, 'tools/msys64'));
const compilerBin = join(msysRoot, 'ucrt64/bin');
const system = join(process.env.SystemRoot ?? 'C:/Windows', 'System32');
const executable = join(root, publicBuild ? 'build/native-public/Fucairserver.exe' : 'build/native/Fucairserver.exe');
const miracasthost = join(root, publicBuild ? 'build/miracast-public/Fucairserver.Miracast.exe' : 'build/miracast/Fucairserver.Miracast.exe');
const executables = [executable, ...(existsSync(miracasthost) ? [miracasthost] : [])];
const hash = file => createHash('sha256').update(readFileSync(file)).digest('hex');
const listing = dir => new Map(readdirSync(dir).map(name => [name.toLowerCase(), join(dir, name)]));
const runtimeFiles = listing(compilerBin), systemFiles = listing(system);
const forbidden = /^(?:adRuntime|adSparkle|airserver(?:events|mediahandlers|mediafoundationobjects)?)\.(?:exe|dll)$/i;
const legacyManifest = JSON.parse(readFileSync(join(root, 'profiles/proprietary-engine-blacklist.json'), 'utf8').replace(/^\uFEFF/, ''));
const forbiddenHashes = new Set(legacyManifest.files.map(item => item.sha256));
const forbiddenCodeHashes = new Set(legacyManifest.files.flatMap(item => (item.code_sections ?? []).map(section => section.sha256)));
function sections(text) {
  return Object.fromEntries(text.replaceAll('\r', '').split('\n\n').filter(part => /^%[A-Z0-9_]+%\n/.test(part)).map(part => {
    const lines = part.trimEnd().split('\n');
    return [lines.shift().slice(1, -1), lines];
  }));
}
const database = join(msysRoot, 'var/lib/pacman/local');
const packageOwners = new Map();
for (const directory of readdirSync(database, { withFileTypes: true })) {
  if (!directory.isDirectory() || !directory.name.startsWith('mingw-w64-ucrt-x86_64-')) continue;
  const location = join(database, directory.name);
  const metadata = sections(readFileSync(join(location, 'desc'), 'utf8'));
  const files = sections(readFileSync(join(location, 'files'), 'utf8')).FILES ?? [];
  const item = { name: metadata.NAME?.[0], version: metadata.VERSION?.[0], source_base: metadata.BASE?.[0], upstream_url: metadata.URL?.[0], licenses: metadata.LICENSE ?? [], license_files: files.filter(path => path.startsWith('ucrt64/share/licenses/') && !path.endsWith('/')) };
  for (const file of files) if (file.startsWith('ucrt64/bin/')) packageOwners.set(file.toLowerCase(), item);
}
const usedPackages = new Map();
mkdirSync(output, { recursive: true });
const queue = [...executables], copied = new Map(), relationships = [], unresolved = [];
while (queue.length) {
  const file = queue.shift();
  const name = basename(file), key = name.toLowerCase();
  if (copied.has(key)) continue;
  if (forbidden.test(name)) throw new Error(`Proprietary receiver import is forbidden: ${name}`);
  const sha256 = hash(file);
  if (forbiddenHashes.has(sha256)) throw new Error(`Original proprietary binary payload is forbidden: ${name}`);
  const pe = inspectPe(file);
  if (pe.machine !== 0x8664) throw new Error(`Unexpected runtime architecture: ${name}`);
  const bytes = readFileSync(file);
  const codeSections = pe.sections.filter(section => section.name === '.text').map(section => ({ name: section.name, sha256: createHash('sha256').update(bytes.subarray(section.raw, section.raw + section.rawSize)).digest('hex'), bytes: section.rawSize }));
  if (codeSections.some(section => forbiddenCodeHashes.has(section.sha256))) throw new Error(`Original proprietary code section detected: ${name}`);
  const owner = packageOwners.get(relative(msysRoot, file).split(sep).join('/').toLowerCase());
  if (!owner && !executables.includes(file)) throw new Error(`Runtime package provenance is missing: ${file}`);
  if (owner) usedPackages.set(owner.name, owner);
  copyFileSync(file, join(output, name));
  copied.set(key, { name, source: file, sha256, bytes: bytes.length, code_sections: codeSections, source_kind: owner ? 'msys2-ucrt64-package' : 'workspace-source-build', source_package: owner ? { name: owner.name, version: owner.version } : null });
  for (const imported of pe.imports) {
    if (forbidden.test(imported)) throw new Error(`Proprietary library requested: ${imported}`);
    const importedKey = imported.toLowerCase();
    if (/^(api|ext)-ms-/.test(importedKey)) { relationships.push({ from: name, library: imported, kind: 'windows-api-set' }); continue; }
    if (runtimeFiles.has(importedKey)) { relationships.push({ from: name, library: imported, kind: 'independent-toolchain-runtime' }); queue.push(runtimeFiles.get(importedKey)); }
    else if (systemFiles.has(importedKey)) relationships.push({ from: name, library: imported, kind: 'windows-system' });
    else unresolved.push({ from: name, library: imported });
  }
}
if (unresolved.length) throw new Error(`Unresolved runtime imports: ${JSON.stringify(unresolved)}`);
if (!publicBuild && existsSync(join(root, 'assets/Fucairserver.ico'))) copyFileSync(join(root, 'assets/Fucairserver.ico'), join(output, 'Fucairserver.ico'));
const licenses = join(output, 'licenses'); mkdirSync(licenses, { recursive: true });
copyFileSync(join(root, 'third_party/AirPlay-Windows/LICENSE'), join(licenses, 'AirPlay-Windows-GPL-3.0.txt'));
copyFileSync(join(root, 'third_party/AirPlay-Windows/THIRD_PARTY_NOTICES.md'), join(licenses, 'AirPlay-Windows-UPSTREAM-NOTICES.md'));
copyFileSync(join(root, 'THIRD_PARTY_NOTICES.md'), join(licenses, 'THIRD_PARTY_NOTICES.md'));
copyFileSync(join(root, 'LICENSE'), join(output, 'LICENSE.txt'));
if (existsSync(join(root, 'docs/USER_GUIDE.md'))) copyFileSync(join(root, 'docs/USER_GUIDE.md'), join(output, 'README.md'));
copyFileSync(join(root, 'third_party/imgui/LICENSE.txt'), join(licenses, 'Dear-ImGui-MIT.txt'));
if (existsSync(miracasthost)) copyFileSync(join(root, 'src/miracast/THIRD_PARTY_NOTICES.md'), join(licenses, 'Miracast-host-NOTICES.md'));
if (existsSync(miracasthost)) copyFileSync(join(root, 'third_party/miracast-reference/research/cppwinrt-tool/LICENSE'), join(licenses, 'Microsoft-CppWinRT-MIT.txt'));
for (const item of usedPackages.values()) {
  for (const path of item.license_files) {
    const target = join(licenses, 'runtime', item.name, path.slice('ucrt64/share/licenses/'.length));
    mkdirSync(dirname(target), { recursive: true });
    copyFileSync(join(msysRoot, path), target);
  }
}
const packages = [...usedPackages.values()].sort((a, b) => a.name.localeCompare(b.name));
writeFileSync(join(licenses, 'runtime-packages.json'), JSON.stringify(packages, null, 2) + '\n');
const sbom = {
  bomFormat: 'CycloneDX', specVersion: '1.6', version: 1,
  metadata: { component: { type: 'application', name: 'Fucairserver', version, licenses: [{ license: { id: 'GPL-3.0-only' } }] } },
  components: packages.map(item => ({
    type: 'library', name: item.name, version: item.version,
    purl: `pkg:generic/msys2/${encodeURIComponent(item.name)}@${encodeURIComponent(item.version)}`,
    licenses: item.licenses.map(license => ({ expression: license.replace(/^spdx:/, '') === 'custom' ? 'LicenseRef-MSYS2-custom' : license.replace(/^spdx:/, '') })),
    externalReferences: [{ type: 'website', url: item.upstream_url }],
  })),
};
const sourceLock = JSON.parse(readFileSync(join(root, 'profiles/source-lock.json'), 'utf8').replace(/^\uFEFF/, ''));
sbom.components.push(...sourceLock.sources.map(source => ({
  type: 'library', name: source.name, version: source.commit,
  licenses: [{ license: { id: source.name === 'Dear ImGui' ? 'MIT' : 'GPL-3.0-only' } }],
  externalReferences: [{ type: 'vcs', url: `${source.url}#${source.commit}` }],
})));
if (existsSync(miracasthost)) {
  const tools = JSON.parse(readFileSync(join(root, 'profiles/build-tools.json'), 'utf8'));
  sbom.components.push({ type: 'framework', name: 'Microsoft.Windows.CppWinRT', version: tools.cppwinrt.version,
    purl: `pkg:nuget/Microsoft.Windows.CppWinRT@${tools.cppwinrt.version}`, licenses: [{ license: { id: 'MIT' } }],
    externalReferences: [{ type: 'distribution', url: tools.cppwinrt.url }],
  });
}
writeFileSync(join(output, 'sbom.cdx.json'), JSON.stringify(sbom, null, 2) + '\n');
writeFileSync(join(output, 'release-manifest.json'), JSON.stringify({
  product: 'Fucairserver', version, architecture: 'x64', minimum_windows_build: 19045,
  public_build: publicBuild, proprietary_icon: !publicBuild && existsSync(join(output, 'Fucairserver.ico')),
  files: [...copied.values()].map(({ name, sha256, bytes, source_package }) => ({ name, sha256, bytes, source_package })),
  sources: JSON.parse(readFileSync(join(root, 'profiles/source-lock.json'), 'utf8').replace(/^\uFEFF/, '')).sources,
}, null, 2) + '\n');
writeFileSync(join(output, 'Launch-Fucairserver.cmd'), '@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0Fucairserver.exe" --listen-port 7010\r\n');
if (existsSync(miracasthost)) writeFileSync(join(output, 'Launch-Wireless-Display.cmd'), '@echo off\r\ncd /d "%~dp0"\r\nstart "" "%~dp0Fucairserver.Miracast.exe" --start --name Fucairserver\r\n');
const report = { packaged_at: new Date().toISOString(), executable: join(output, 'Fucairserver.exe'), source_lock: JSON.parse(readFileSync(join(root, 'profiles/source-lock.json'), 'utf8').replace(/^\uFEFF/, '')), files: [...copied.values()], runtime_packages: packages, relationships, unresolved, proprietary_airserver_files: 0, proprietary_code_section_matches: 0, icon_policy: 'original' };
const reportPath = join(root, 'build', `package-provenance-${basename(output)}.json`);
mkdirSync(dirname(reportPath), { recursive: true });
writeFileSync(reportPath, JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({ output, version, public_build: publicBuild, native_files: copied.size, native_bytes: [...copied.values()].reduce((sum, item) => sum + item.bytes, 0), proprietary_airserver_files: 0 }));
