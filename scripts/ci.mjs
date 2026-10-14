import { spawnSync } from 'node:child_process';
import { cpSync, mkdirSync, readFileSync, statSync } from 'node:fs';
import { resolve, join } from 'node:path';

const root = process.cwd();
function check(command, args, marker) {
  const result = spawnSync(command, args, { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024, timeout: 1200000 });
  process.stdout.write(result.stdout || '');
  process.stderr.write(result.stderr || '');
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} exited ${result.status}`);
  const output = (result.stdout || '') + (result.stderr || '');
  if (marker && !output.includes(marker)) throw new Error(`${command} did not report ${marker}`);
  return output;
}
check(process.execPath, ['--test', '--test-reporter=tap', 'tests/package.test.cjs'], '# fail 0');
const tests = check('ctest', ['--test-dir', 'build', '-C', 'Release', '-V', '--output-on-failure', '--no-tests=error'], 'RENDER GATE PASS');
if (/\*\*\*Skipped|Not Run|SKIP:/i.test(tests)) throw new Error('Required native tests were skipped');
const binary = resolve(process.platform === 'win32' ? 'build/Release/glistening.exe' : 'build/glistening');
check(binary, ['--version']);
check(binary, ['--self-test']);
const fixture = join(root, 'build', 'audio fixture.wav');
check('ffmpeg', ['-nostdin', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=1', fixture]);
const frame = join(root, 'build', 'file-smoke.ppm');
check(binary, ['--file', fixture, '--mute', '--no-director', '--dump', frame, '--seconds', '0.5', '--size', '320x180']);
if (!statSync(frame).isFile() || !readFileSync(frame).subarray(0, 2).equals(Buffer.from('P6'))) throw new Error('File smoke produced no PPM frame');
const target = `${process.platform}-${process.arch}`;
const destination = join(root, 'artifacts', target);
mkdirSync(destination, { recursive: true });
cpSync(binary, join(destination, process.platform === 'win32' ? 'glistening.exe' : 'glistening'));
console.log('NATIVE CI PASS');
