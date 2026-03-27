// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('fs');
const path = require('path');

function prependPathEntries(entries) {
  const existing = (process.env.PATH || '').split(';').filter(Boolean);
  for (const entry of entries) {
    if (!entry || !fs.existsSync(entry)) continue;
    if (!existing.includes(entry)) existing.unshift(entry);
  }
  process.env.PATH = existing.join(';');
}

function loadNative() {
  try {
    if (process.platform === 'linux') {
      const addonDir = path.join(__dirname, '..', 'build', 'Release');
      const coreDir = path.join(__dirname, '..', '..', '..', 'core', 'build', 'lib');
      const coreAltDir = path.join(__dirname, '..', '..', 'build_cpp', 'lib');
      const addonLib = path.join(addonDir, 'libzlink.so.5');
      const coreLib = path.join(coreDir, 'libzlink.so.5');
      const coreAltLib = path.join(coreAltDir, 'libzlink.so.5');
      if (!fs.existsSync(addonLib)) {
        let sourceLib = null;
        if (fs.existsSync(coreAltLib)) {
          sourceLib = coreAltLib;
        } else if (fs.existsSync(coreLib)) {
          sourceLib = coreLib;
        }
        if (sourceLib) {
          try {
            fs.symlinkSync(sourceLib, addonLib);
          } catch (err) {
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
    return require('../build/Release/zlink.node');
  } catch (_) {
    try {
      const prebuiltDir = path.join(__dirname, '..', 'prebuilds', `${process.platform}-${process.arch}`);
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
      return require(prebuilt);
    } catch (_) {
      return null;
    }
  }
}

const native = loadNative();

function requireNative() {
  if (!native) throw new Error('zlink native addon not found. Build with node-gyp.');
  return native;
}

module.exports = {
  requireNative
};
