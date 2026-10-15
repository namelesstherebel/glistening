'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { copyFileSync, mkdtempSync, mkdirSync, writeFileSync, rmSync } = require('node:fs');
const { tmpdir } = require('node:os');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');
const { binaryPath } = require('../bin/glistening.cjs');

// Hosts without a prebuilt target (linux-arm64, win32-arm64, ...) still run the suite.
const hostSupported = (() => { try { binaryPath('/'); return true; } catch { return false; } })();

test('supported platforms select native executables; unsupported architecture fails clearly', () => {
  for (const [os, arch] of [['linux', 'x64'], ['darwin', 'arm64'], ['darwin', 'x64'], ['win32', 'x64']]) {
    assert.equal(binaryPath('/bundle', os, arch), path.join('/bundle', 'prebuilt', `${os}-${arch}`, os === 'win32' ? 'glistening.exe' : 'glistening'));
  }
  assert.throws(() => binaryPath('/bundle', 'win32', 'arm64'), /No prebuilt binary/);
  assert.throws(() => binaryPath('/bundle', '../../escape', 'x64'), /No prebuilt binary/);
});

test('missing native binary fails with an actionable message', () => {
  // A copy of the launcher in an empty root: a local prebuilt/ cannot satisfy it.
  const root = mkdtempSync(path.join(tmpdir(), 'glistening-empty-'));
  try {
    const launcher = path.join(root, 'bin', 'glistening.cjs');
    mkdirSync(path.dirname(launcher));
    copyFileSync(path.resolve(__dirname, '../bin/glistening.cjs'), launcher);
    const result = spawnSync(process.execPath, [launcher, '--version'], { encoding: 'utf8' });
    assert.equal(result.status, 1);
    assert.match(result.stderr, hostSupported ? /Native binary missing/ : /No prebuilt binary/);
  } finally { rmSync(root, { recursive: true, force: true }); }
});

test('dependency setup dry-run needs no binary and performs no installation', () => {
  const result = spawnSync(process.execPath, [path.resolve(__dirname, '../bin/glistening.cjs'), 'setup', '--dry-run'], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /ffmpeg|FFmpeg/);
  assert.doesNotMatch(result.stdout, /installation completed/);
});

test('launcher preserves paths, shell metacharacters and native exit status', { skip: process.platform === 'win32' || !hostSupported }, () => {
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

test('Ctrl+C waits for the native child to shut down', { skip: process.platform === 'win32' || !hostSupported }, async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'glistening-sigint-'));
  try {
    const binary = binaryPath(root);
    mkdirSync(path.dirname(binary), { recursive: true });
    // Like the real child: traps SIGINT, cleans up for a moment, then exits.
    writeFileSync(binary, '#!/bin/sh\ntrap \'sleep 0.3; echo cleaned; exit 0\' INT\necho ready\nwhile :; do sleep 0.05; done\n', { mode: 0o755 });
    const code = 'process.exitCode = require(process.argv[1]).run(process.argv[2], [])';
    // Its own process group stands in for the terminal's foreground group.
    const launcher = spawn(process.execPath, ['-e', code, path.resolve(__dirname, '../bin/glistening.cjs'), root], { detached: true });
    let out = '';
    const exited = new Promise((resolve) => launcher.on('close', (status, signal) => resolve({ status, signal, out })));
    launcher.stdout.on('data', (d) => {
      out += d;
      if (out === 'ready\n') process.kill(-launcher.pid, 'SIGINT');
    });
    const result = await exited;
    assert.equal(result.signal, null);
    assert.equal(result.status, 0);
    assert.equal(result.out, 'ready\ncleaned\n');
  } finally { rmSync(root, { recursive: true, force: true }); }
});
