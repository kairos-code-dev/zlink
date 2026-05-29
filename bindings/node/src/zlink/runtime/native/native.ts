// SPDX-License-Identifier: MPL-2.0

import * as fs from 'node:fs';
import * as path from 'node:path';

export interface NativeBinding {
  [name: string]: (...args: unknown[]) => unknown;
}

interface NativeLoadFailure {
  target: string;
  error: unknown;
}

function describeLoadFailure(failure: NativeLoadFailure): string {
  const message = failure.error instanceof Error
    ? failure.error.message
    : String(failure.error);
  return `${failure.target}: ${message}`;
}

function refreshAddonRuntimeLink(
  addonLib: string,
  sourceLibs: string[]
): void {
  for (const sourceLib of sourceLibs) {
    if (!fs.existsSync(sourceLib)) continue;

    const sourceReal = fs.realpathSync(sourceLib);
    const currentReal = fs.existsSync(addonLib) ? fs.realpathSync(addonLib) : null;
    if (currentReal === sourceReal) return;

    fs.rmSync(addonLib, { force: true });
    fs.symlinkSync(sourceLib, addonLib);
    return;
  }
}

function prependPathEntries(entries: Array<string | undefined>): void {
  const existing = (process.env.PATH || '').split(';').filter(Boolean);
  for (const entry of entries) {
    if (!entry || !fs.existsSync(entry)) continue;
    if (!existing.includes(entry)) existing.unshift(entry);
  }
  process.env.PATH = existing.join(';');
}

function loadNative(): NativeBinding {
  const packageRoot = path.join(__dirname, '..', '..', '..', '..');
  const failures: NativeLoadFailure[] = [];
  const buildAddon = path.join(packageRoot, 'build', 'Release', 'zlink.node');
  try {
    if (process.platform === 'linux') {
      const addonDir = path.join(packageRoot, 'build', 'Release');
      const coreDir = path.join(packageRoot, '..', '..', 'core', 'build', 'lib');
      const coreAltDir = path.join(packageRoot, '..', 'build_cpp', 'lib');
      const addonLib = path.join(addonDir, 'libzlink.so.6');
      const coreLib = path.join(coreDir, 'libzlink.so.6');
      const coreAltLib = path.join(coreAltDir, 'libzlink.so.6');
      refreshAddonRuntimeLink(addonLib, [coreLib, coreAltLib]);
      const existing = (process.env.LD_LIBRARY_PATH || '').split(':').filter(Boolean);
      for (const entry of [coreDir, coreAltDir, addonDir]) {
        if (!existing.includes(entry)) existing.unshift(entry);
      }
      process.env.LD_LIBRARY_PATH = existing.join(':');
    }
    return require(buildAddon) as NativeBinding;
  } catch (error) {
    failures.push({ target: buildAddon, error });
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
    } catch (error) {
      failures.push({
        target: path.join(packageRoot, 'prebuilds', `${process.platform}-${process.arch}`, 'zlink.node'),
        error
      });
    }
  }
  throw new Error([
    'zlink native addon not found. Build with node-gyp.',
    ...failures.map((failure) => `- ${describeLoadFailure(failure)}`)
  ].join('\n'));
}

let native: NativeBinding | null = null;
let loadError: Error | null = null;
try {
  native = loadNative();
} catch (error) {
  loadError = error instanceof Error ? error : new Error(String(error));
}

export function requireNative(): NativeBinding {
  if (!native) throw loadError ?? new Error('zlink native addon not found. Build with node-gyp.');
  return native;
}
