'use strict';
const fs = require('node:fs');
const { spawnSync } = require('node:child_process');

function commands(platform = process.platform, release = '') {
  if (platform === 'darwin') {
    const brew = ['/opt/homebrew/bin/brew', '/usr/local/bin/brew'].find(fs.existsSync) || 'brew';
    return [[brew, 'install', 'ffmpeg']];
  }
  if (platform === 'win32') return [['winget', 'install', '--id', 'Gyan.FFmpeg', '--exact', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements']];
  if (platform !== 'linux') throw new Error(`Dependency setup does not support ${platform}`);
  const id = release.match(/^ID=(.+)$/m)?.[1]?.replaceAll('"', '') || '';
  const like = release.match(/^ID_LIKE=(.+)$/m)?.[1]?.replaceAll('"', '') || '';
  const distros = `${id} ${like}`.split(/\s+/);
  if (distros.some(id => ['debian', 'ubuntu'].includes(id))) return [['sudo', 'apt-get', 'update'], ['sudo', 'apt-get', 'install', '-y', 'ffmpeg', 'pulseaudio-utils', 'libegl1', 'libgl1-mesa-dri']];
  if (distros.includes('arch')) return [['sudo', 'pacman', '-S', '--needed', 'ffmpeg', 'libpulse', 'mesa']];
  if (distros.some(id => ['fedora', 'rhel'].includes(id))) return [['sudo', 'dnf', 'install', '-y', 'ffmpeg-free', 'pulseaudio-utils', 'mesa-libEGL', 'mesa-dri-drivers']];
  throw new Error('Install FFmpeg, parec (PulseAudio client tools), and EGL/OpenGL 3.3 through your distribution package manager. Automatic setup supports Debian/Ubuntu, Arch, and Fedora.');
}

function setup(args) {
  if (args.some(arg => arg !== '--dry-run')) throw new Error('Usage: glistening setup [--dry-run]');
  const release = process.platform === 'linux' ? fs.readFileSync('/etc/os-release', 'utf8') : '';
  for (const [command, ...argv] of commands(process.platform, release)) {
    console.log([command, ...argv].join(' '));
    if (args.includes('--dry-run')) continue;
    const result = spawnSync(command, argv, { stdio: 'inherit' });
    if (result.error?.code === 'ENOENT') throw new Error(`${command} is required. macOS: install Homebrew from https://brew.sh ; Windows: install App Installer (winget) from Microsoft Store.`);
    if (result.error) throw result.error;
    if (result.status !== 0) return result.status || 1;
  }
  if (!args.includes('--dry-run')) console.log('Dependency installation completed. On Windows, reopen your terminal to refresh PATH. macOS live capture requests System Audio Recording permission on first use.');
  return 0;
}
module.exports = { commands, setup };
