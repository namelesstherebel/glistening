// Exercise the real npm archive and launcher from outside the source checkout.
import { cpSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { resolve, join, dirname } from 'node:path';
import { spawnSync } from 'node:child_process';
import { validateBundle } from './package-contents.mjs';

const root = process.cwd();
const pkg = JSON.parse(readFileSync('package.json', 'utf8'));
const target = `${process.platform}-${process.arch}`;
const filename = process.platform === 'win32' ? 'glistening.exe' : 'glistening';
const stage = resolve('build/package smoke');
const installed = resolve('build/installed package');
const suppliedArchive = process.argv[2] && resolve(process.argv[2]);
if (process.argv.length > 3) throw new Error('Usage: npm run test:install -- [bundle.tgz]');
if (!suppliedArchive) {
  rmSync(stage, { recursive: true, force: true });
  mkdirSync(stage, { recursive: true });
  for (const name of ['bin', 'LICENSE', 'README.md']) cpSync(name, join(stage, name), { recursive: true });
  const native = join(stage, 'prebuilt', target, filename);
  mkdirSync(dirname(native), { recursive: true });
  cpSync(join('artifacts', target, filename), native);
  writeFileSync(join(stage, 'package.json'), JSON.stringify({ ...pkg, scripts: undefined }, null, 2));
}
/** Execute an installed-package smoke command in its working directory and return stdout. */
function run(command, args, cwd = root) {
  const result = spawnSync(command, args, { cwd, encoding: 'utf8', timeout: 300000, maxBuffer: 4 * 1024 * 1024 });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed: ${result.stderr}\n${result.stdout}`);
  return result.stdout;
}
const npm = process.env.npm_execpath;
if (!npm) throw new Error('Run with npm run test:install');
let archive = suppliedArchive;
if (!archive) {
  const packed = JSON.parse(run(process.execPath, [npm, 'pack', '--json'], stage))[0];
  archive = join(stage, packed.filename);
}
rmSync(installed, { recursive: true, force: true });
run(process.execPath, [npm, 'install', '--ignore-scripts', '--no-audit', '--no-fund', '--prefix', installed, archive]);
const installedRoot = join(installed, 'node_modules', 'glistening');
if (suppliedArchive) {
  validateBundle(installedRoot);
  console.log('BUNDLE CONTENTS PASS');
}
if (process.platform === 'win32') {
  // Test-only llvmpipe for the runner VM. End users use their normal GPU driver.
  for (const dll of ['opengl32.dll', 'libgallium_wgl.dll']) cpSync(join(root, 'build', 'Release', dll), join(installedRoot, 'prebuilt', target, dll));
}
const launcher = join(installedRoot, 'bin', 'glistening.cjs');
console.log(run(process.execPath, [launcher, '--version'], installed));
console.log(run(process.execPath, [launcher, '--self-test'], installed));
console.log('INSTALL SMOKE PASS');
