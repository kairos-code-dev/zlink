#!/usr/bin/env node
// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const packageRoot = path.resolve(__dirname, '..');
const prebuildRoot = path.join(packageRoot, 'prebuilds');
const packageVersion = JSON.parse(
  fs.readFileSync(path.join(packageRoot, 'package.json'), 'utf8')
).version;

function fail(message) {
  throw new Error(message);
}

function commandOutput(command, args) {
  return execFileSync(command, args, { encoding: 'utf8' });
}

function fileDescription(target) {
  return commandOutput('file', [target]).trim();
}

function readElfDynamic(target) {
  return commandOutput('readelf', ['-d', target]);
}

function validateLinux(dir, arch) {
  const addon = path.join(dir, 'zlink.node');
  const dynamic = readElfDynamic(addon);
  if (!dynamic.includes('Shared library: [libzlink.so.6]')) {
    fail(`${addon} must depend on libzlink.so.6`);
  }
  if (dynamic.includes('Shared library: [libzlink.so.5]')) {
    fail(`${addon} still depends on stale libzlink.so.5`);
  }
  if (!dynamic.includes('Library runpath: [$ORIGIN]')) {
    fail(`${addon} must use $ORIGIN runpath`);
  }
  const linuxCoreLib = `libzlink.so.${packageVersion}`;
  if (!fs.existsSync(path.join(dir, linuxCoreLib))) {
    fail(`${dir} is missing ${linuxCoreLib}`);
  }
  for (const stale of [
    'libzlink.so.6.0.0',
    'libzlink.so.6.0.1',
    'libzlink_c.so',
    'libzlink_c.so.1',
    'libzlink_c.so.1.0.0',
  ]) {
    if (fs.existsSync(path.join(dir, stale))) {
      fail(`${dir} contains stale ${stale}`);
    }
  }
  const description = fileDescription(addon);
  if (arch === 'x64' && !description.includes('x86-64')) {
    fail(`${addon} is not x86-64: ${description}`);
  }
  if (arch === 'arm64' && !description.includes('aarch64')) {
    fail(`${addon} is not aarch64: ${description}`);
  }
}

function validateDarwin(dir, arch) {
  const addon = path.join(dir, 'zlink.node');
  const description = fileDescription(addon);
  if (arch === 'arm64' && !description.includes('arm64')) {
    fail(`${addon} is not arm64: ${description}`);
  }
  if (arch === 'x64' && !description.includes('x86_64')) {
    fail(`${addon} is not x86_64: ${description}`);
  }
}

function validateWindows(dir, arch) {
  const expected = arch === 'arm64' ? 'Aarch64' : arch === 'x64' ? 'x86-64' : null;
  if (!expected) {
    fail(`unsupported Windows prebuild arch: ${arch}`);
  }

  for (const name of fs.readdirSync(dir).sort()) {
    if (!name.endsWith('.dll') && !name.endsWith('.node')) continue;
    const target = path.join(dir, name);
    const description = fileDescription(target);
    if (!description.includes(expected)) {
      fail(`${target} is not ${expected}: ${description}`);
    }
  }
}

function validateDir(entry) {
  const dir = path.join(prebuildRoot, entry);
  const addon = path.join(dir, 'zlink.node');
  if (!fs.existsSync(addon)) {
    fail(`${entry} is missing zlink.node`);
  }

  const [platform, arch] = entry.split('-');
  if (platform === 'linux') {
    validateLinux(dir, arch);
  } else if (platform === 'darwin') {
    validateDarwin(dir, arch);
  } else if (platform === 'win32') {
    validateWindows(dir, arch);
  } else {
    fail(`unsupported prebuild platform directory: ${entry}`);
  }
}

const entries = fs.readdirSync(prebuildRoot)
  .filter((entry) => fs.statSync(path.join(prebuildRoot, entry)).isDirectory())
  .sort();

for (const entry of entries) {
  validateDir(entry);
}

console.log(`[prebuilds] verified ${entries.join(', ')}`);
