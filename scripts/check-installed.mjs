// Exercise the real npm archive and launcher from outside the source checkout.
import { cpSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve, join, dirname } from 'node:path';
import { spawnSync } from 'node:child_process';

const root = process.cwd();
const pkg = JSON.parse(readFileSync('package.json', 'utf8'));
const target = `${process.platform}-${process.arch}`;
const filename = process.platform === 'win32' ? 'glistening.exe' : 'glistening';
const stage = resolve('build/package smoke');
const installed = resolve('build/installed package');
mkdirSync(stage, { recursive: true });
for (const name of ['bin', 'LICENSE', 'README.md']) cpSync(name, join(stage, name), { recursive: true });
const native = join(stage, 'prebuilt', target, filename);
mkdirSync(dirname(native), { recursive: true });
cpSync(join('artifacts', target, filename), native);
writeFileSync(join(stage, 'package.json'), JSON.stringify({ ...pkg, scripts: undefined }, null, 2));
function run(command, args, cwd = root) {
  const result = spawnSync(command, args, { cwd, encoding: 'utf8', timeout: 300000, maxBuffer: 4 * 1024 * 1024 });
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed: ${result.stderr}\n${result.stdout}`);
  return result.stdout;
}
const npm = process.env.npm_execpath;
if (!npm) throw new Error('Run with npm run test:install');
const packed = JSON.parse(run(process.execPath, [npm, 'pack', '--json'], stage))[0];
for (const file of packed.files) {
  if (!/^(package\.json|LICENSE|README\.md|bin\/[\w.-]+\.cjs|prebuilt\/(linux-x64|darwin-(arm64|x64)|win32-x64)\/glistening(\.exe)?)$/.test(file.path)) {
    throw new Error(`Unexpected npm file: ${file.path}`);
  }
}
run(process.execPath, [npm, 'install', '--ignore-scripts', '--no-audit', '--no-fund', '--prefix', installed, join(stage, packed.filename)]);
const installedRoot = join(installed, 'node_modules', 'glistening');
if (process.platform === 'win32') {
  // Test-only llvmpipe for the runner VM. End users use their normal GPU driver.
  for (const dll of ['opengl32.dll', 'libgallium_wgl.dll']) cpSync(join(root, 'build', 'Release', dll), join(installedRoot, 'prebuilt', target, dll));
}
const launcher = join(installedRoot, 'bin', 'glistening.cjs');
console.log(run(process.execPath, [launcher, '--version'], installed));
console.log(run(process.execPath, [launcher, '--self-test'], installed));
console.log('INSTALL SMOKE PASS');
