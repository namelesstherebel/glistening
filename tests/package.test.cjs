'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { mkdtempSync, mkdirSync, writeFileSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { binaryPath } = require('../bin/glistening.cjs');

test('supported platforms select native executables; unsupported architecture fails clearly', () => {
  for (const [os, arch] of [['linux', 'x64'], ['darwin', 'arm64'], ['darwin', 'x64'], ['win32', 'x64']]) {
    assert.equal(binaryPath('/bundle', os, arch), path.join('/bundle', 'prebuilt', `${os}-${arch}`, os === 'win32' ? 'glistening.exe' : 'glistening'));
  }
  assert.throws(() => binaryPath('/bundle', 'win32', 'arm64'), /No prebuilt binary/);
  assert.throws(() => binaryPath('/bundle', '../../escape', 'x64'), /No prebuilt binary/);
});

test('source checkout fails with actionable missing-release message', () => {
  const result = spawnSync(process.execPath, [path.resolve(__dirname, '../bin/glistening.cjs'), '--version'], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Native binary missing/);
});

test('dependency setup dry-run needs no binary and performs no installation', () => {
  const result = spawnSync(process.execPath, [path.resolve(__dirname, '../bin/glistening.cjs'), 'setup', '--dry-run'], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /ffmpeg|FFmpeg/);
  assert.doesNotMatch(result.stdout, /installation completed/);
});

test('launcher preserves paths, shell metacharacters and native exit status', { skip: process.platform === 'win32' }, () => {
  const root = mkdtempSync(path.join(tmpdir(), 'glistening package '));
  try {
    const binary = binaryPath(root);
    mkdirSync(path.dirname(binary), { recursive: true });
    writeFileSync(binary, '#!/bin/sh\nprintf "%s\\n" "$1"\nexit 17\n', { mode: 0o755 });
    const launcher = path.resolve(__dirname, '../bin/glistening.cjs');
    const input = 'a song; $(do-not-execute) "quote"';
    const code = 'process.exitCode = require(process.argv[1]).run(process.argv[2], [process.argv[3]])';
    const result = spawnSync(process.execPath, ['-e', code, launcher, root, input], { encoding: 'utf8' });
    assert.equal(result.status, 17);
    assert.equal(result.stdout, input + '\n');
  } finally { rmSync(root, { recursive: true, force: true }); }
});
