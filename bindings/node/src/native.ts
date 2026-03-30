// SPDX-License-Identifier: MPL-2.0

import * as fs from 'node:fs';
import * as path from 'node:path';

export interface NativeBinding {
  [name: string]: (...args: any[]) => any;
}

function prependPathEntries(entries: Array<string | undefined>): void {
  const existing = (process.env.PATH || '').split(';').filter(Boolean);
  for (const entry of entries) {
    if (!entry || !fs.existsSync(entry)) continue;
    if (!existing.includes(entry)) existing.unshift(entry);
  }
  process.env.PATH = existing.join(';');
}

function loadNative(): NativeBinding | null {
  const packageRoot = path.join(__dirname, '..');
  try {
    if (process.platform === 'linux') {
      const addonDir = path.join(packageRoot, 'build', 'Release');
      const coreDir = path.join(packageRoot, '..', '..', 'core', 'build', 'lib');
      const coreAltDir = path.join(packageRoot, '..', 'build_cpp', 'lib');
      const addonLib = path.join(addonDir, 'libzlink.so.5');
      const coreLib = path.join(coreDir, 'libzlink.so.5');
      const coreAltLib = path.join(coreAltDir, 'libzlink.so.5');
      if (!fs.existsSync(addonLib)) {
        let sourceLib: string | null = null;
        if (fs.existsSync(coreAltLib)) {
          sourceLib = coreAltLib;
        } else if (fs.existsSync(coreLib)) {
          sourceLib = coreLib;
        }
        if (sourceLib) {
          try {
            fs.symlinkSync(sourceLib, addonLib);
          } catch (err: any) {
            if (!err || err.code !== 'EEXIST') throw err;
          }
        }
      }
      const existing = (process.env.LD_LIBRARY_PATH || '').split(':').filter(Boolean);
      for (const entry of [coreAltDir, coreDir, addonDir]) {
        if (!existing.includes(entry)) existing.unshift(entry);
      }
      process.env.LD_LIBRARY_PATH = existing.join(':');
    }
    return require(path.join(packageRoot, 'build', 'Release', 'zlink.node')) as NativeBinding;
  } catch (_) {
    try {
      const prebuiltDir = path.join(
        packageRoot,
        'prebuilds',
        `${process.platform}-${process.arch}`
      );
      const prebuilt = path.join(prebuiltDir, 'zlink.node');
      if (process.platform === 'win32') {
        prependPathEntries([
          prebuiltDir,
          process.env.ZLINK_OPENSSL_BIN,
          process.env.OPENSSL_BIN,
          'C:\\Program Files\\OpenSSL-Win64\\bin',
          'C:\\Program Files\\Git\\mingw64\\bin',
          'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\TeamFoundation\\Team Explorer\\Git\\mingw64\\bin'
        ]);
      }
      return require(prebuilt) as NativeBinding;
    } catch (_) {
      return null;
    }
  }
}

const native = loadNative();

export function requireNative(): NativeBinding {
  if (!native) throw new Error('zlink native addon not found. Build with node-gyp.');
  return native;
}
