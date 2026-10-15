// Assemble an npm package from tested CI artifacts; never fetch or compile at install time.
import { cpSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { targets, validateNative } from './package-contents.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const artifacts = resolve(process.argv[2] || 'artifacts');
const output = resolve(process.argv[3] || 'dist');
const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const hashes = {};
const files = targets.map(target => {
  const name = target.startsWith('win32') ? 'glistening.exe' : 'glistening';
  const source = join(artifacts, target, name);
  if (!statSync(source).isFile() || statSync(source).size < 10000) throw new Error(`Missing native executable: ${source}`);
  const data = readFileSync(source);
  validateNative(data, target);
  hashes[`${target}/${name}`] = createHash('sha256').update(data).digest('hex');
  return { target, name, source };
});
mkdirSync(output, { recursive: true });
const staging = join(output, `glistening-${pkg.version}`);
mkdirSync(staging); // Refuse to reuse a directory containing stale binaries.
for (const { target, name, source } of files) {
  const destination = join(staging, 'prebuilt', target);
  mkdirSync(destination, { recursive: true });
  cpSync(source, join(destination, name));
}
for (const name of ['bin', 'LICENSE', 'README.md']) cpSync(join(root, name), join(staging, name), { recursive: true });
writeFileSync(join(staging, 'package.json'), JSON.stringify({ ...pkg, scripts: undefined }, null, 2) + '\n');
writeFileSync(join(staging, 'prebuilt', 'SHA256SUMS.json'), JSON.stringify(hashes, null, 2) + '\n');
const result = spawnSync(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['pack', '--pack-destination', output], { cwd: staging, stdio: 'inherit', shell: process.platform === 'win32' });
if (result.error) throw result.error;
if (result.status !== 0) process.exit(result.status || 1);
console.log('BUNDLE PASS');
