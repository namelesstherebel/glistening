// Validate actual release contents, including target architecture and manifest.
import { readFileSync, readdirSync } from 'node:fs';
import { join, relative } from 'node:path';
import { createHash } from 'node:crypto';

export const targets = ['linux-x64', 'darwin-arm64', 'darwin-x64', 'win32-x64'];
export const executableName = target => target === 'win32-x64' ? 'glistening.exe' : 'glistening';

export function validateNative(data, target) {
  let valid = false;
  if (data.length >= 10000) {
    if (target === 'linux-x64') valid = data.subarray(0, 4).equals(Buffer.from([127, 69, 76, 70])) && data[4] === 2 && data[5] === 1 && data.readUInt16LE(18) === 62;
    if (target.startsWith('darwin-')) valid = data.readUInt32LE(0) === 0xfeedfacf && data.readUInt32LE(4) === (target === 'darwin-arm64' ? 0x0100000c : 0x01000007);
    if (target === 'win32-x64' && data.toString('ascii', 0, 2) === 'MZ') {
      const pe = data.readUInt32LE(0x3c);
      valid = pe <= data.length - 6 && data.toString('ascii', pe, pe + 4) === 'PE\0\0' && data.readUInt16LE(pe + 4) === 0x8664;
    }
  }
  if (!valid) throw new Error(`Invalid native executable for ${target}`);
}

export function validateBundle(root) {
  const hashes = JSON.parse(readFileSync(join(root, 'prebuilt', 'SHA256SUMS.json'), 'utf8'));
  const expected = targets.map(target => `${target}/${executableName(target)}`).sort();
  if (JSON.stringify(Object.keys(hashes).sort()) !== JSON.stringify(expected)) throw new Error('Bundle manifest must cover every release target exactly once');
  for (const target of targets) {
    const name = `${target}/${executableName(target)}`;
    const data = readFileSync(join(root, 'prebuilt', name));
    validateNative(data, target);
    const actual = createHash('sha256').update(data).digest('hex');
    if (hashes[name] !== actual) throw new Error(`Bundle checksum mismatch: ${name}`);
  }
  const allowed = new Set(['package.json', 'LICENSE', 'README.md', 'bin/glistening.cjs', 'bin/setup.cjs', 'prebuilt/SHA256SUMS.json', ...expected.map(name => `prebuilt/${name}`)]);
  function walk(dir) {
    for (const item of readdirSync(dir, { withFileTypes: true })) {
      const file = join(dir, item.name);
      const name = relative(root, file).replaceAll('\\', '/');
      if (item.isDirectory()) walk(file);
      else if (!item.isFile() || !allowed.has(name)) throw new Error(`Unexpected npm file: ${name}`);
    }
  }
  walk(root);
}
