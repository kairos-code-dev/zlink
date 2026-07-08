const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const packageRoot = path.dirname(path.dirname(require.resolve('@zlink-systems/zlink')));

test('loaded node binding package provides a native artifact for the current platform', () => {
  const artifact = activeNativeArtifact();

  assert.ok(artifact, 'expected a native addon artifact for the current platform');
});

function activeNativeArtifact() {
  const candidates = [
    path.join(packageRoot, 'build', 'Release', 'zlink.node'),
    path.join(packageRoot, 'prebuilds', `${process.platform}-${process.arch}`, 'zlink.node')
  ];
  for (const file of candidates) {
    if (fs.existsSync(file)) {
      return file;
    }
  }
  return null;
}
