'use strict';

function loadNative() {
  try {
    return require('../build/Release/zlink.node');
  } catch (err) {
    return null;
  }
}

const native = loadNative();

function requireNative() {
  if (!native) {
    throw new Error('zlink native addon not found. Build with node-gyp and set ZLINK_LIB_DIR if needed.');
  }
  return native;
}

function version() {
  return requireNative().version();
}

module.exports = {
  version
};
