#!/usr/bin/env node
'use strict';
const path = require('node:path');
const fs = require('node:fs');
const { spawn } = require('node:child_process');

function binaryPath(root, platform = process.platform, arch = process.arch) {
  const target = `${platform}-${arch}`;
  if (!['linux-x64', 'darwin-arm64', 'darwin-x64', 'win32-x64'].includes(target)) {
    throw new Error(`No prebuilt binary for ${target}. Build from source: https://github.com/namelesstherebel/glistening`);
  }
  return path.join(root, 'prebuilt', target, platform === 'win32' ? 'glistening.exe' : 'glistening');
}

async function run(root, args) {
  if (args[0] === 'setup') return require('./setup.cjs').setup(args.slice(1));
  const binary = binaryPath(root);
  if (!fs.existsSync(binary)) throw new Error('Native binary missing. Install a release bundle from https://github.com/namelesstherebel/glistening/releases');
  const child = spawn(binary, args, { stdio: 'inherit', windowsHide: false });
  // The child quits cleanly on these, so the launcher waits for it: its shutdown
  // restores the terminal. Ctrl+C reaches the child from the terminal too; a
  // signal sent to the launcher alone is passed on. Windows delivers console
  // events (Ctrl+C, Ctrl+Break, close) to both processes, and kill() there
  // would end the child abruptly.
  const signals = ['SIGINT', 'SIGTERM', 'SIGHUP', 'SIGBREAK'];
  const relay = (signal) => { if (process.platform !== 'win32') child.kill(signal); };
  for (const signal of signals) process.on(signal, relay);
  try {
    return await new Promise((resolve, reject) => {
      child.on('error', reject);
      child.on('exit', (status, signal) => resolve(signal ? (signal === 'SIGINT' ? 130 : 1) : status ?? 1));
    });
  } finally {
    for (const signal of signals) process.off(signal, relay);
  }
}

if (require.main === module) {
  run(path.resolve(__dirname, '..'), process.argv.slice(2)).then(
    (status) => { process.exitCode = status; },
    (error) => { console.error(`glistening: ${error.message}`); process.exitCode = 1; });
}
module.exports = { binaryPath, run };
