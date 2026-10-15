#!/usr/bin/env node
'use strict';
const path = require('node:path');
const fs = require('node:fs');
const { spawnSync } = require('node:child_process');

function binaryPath(root, platform = process.platform, arch = process.arch) {
  const target = `${platform}-${arch}`;
  if (!['linux-x64', 'darwin-arm64', 'darwin-x64', 'win32-x64'].includes(target)) {
    throw new Error(`No prebuilt binary for ${target}. Build from source: https://github.com/namelesstherebel/glistening`);
  }
  return path.join(root, 'prebuilt', target, platform === 'win32' ? 'glistening.exe' : 'glistening');
}

function run(root, args) {
  if (args[0] === 'setup') return require('./setup.cjs').setup(args.slice(1));
  const binary = binaryPath(root);
  if (!fs.existsSync(binary)) throw new Error('Native binary missing. Install a release bundle from https://github.com/namelesstherebel/glistening/releases');
  // The terminal sends Ctrl+C (and Ctrl+\) to the child too. Ignore it here so
  // the launcher outlives the child's own shutdown, which restores the terminal.
  const ignore = () => {};
  process.on('SIGINT', ignore);
  process.on('SIGQUIT', ignore);
  let result;
  try { result = spawnSync(binary, args, { stdio: 'inherit', windowsHide: false }); }
  finally { process.off('SIGINT', ignore); process.off('SIGQUIT', ignore); }
  if (result.error) throw result.error;
  if (result.signal) return result.signal === 'SIGINT' ? 130 : 1;
  return result.status ?? 1;
}

if (require.main === module) {
  try { process.exitCode = run(path.resolve(__dirname, '..'), process.argv.slice(2)); }
  catch (error) { console.error(`glistening: ${error.message}`); process.exitCode = 1; }
}
module.exports = { binaryPath, run };
