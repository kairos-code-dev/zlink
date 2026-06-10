const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const repoRoot = path.resolve(__dirname, '..', '..', '..', '..', '..');
const bindingRoot = path.join(repoRoot, 'bindings', 'node');

test('loaded node binding native artifact is not older than native sources', () => {
  const artifact = activeNativeArtifact();
  const newestSource = newestNativeSource();

  assert.ok(artifact, 'expected a native addon artifact for the current platform');
  assert.ok(newestSource, 'expected native source files to compare');
  assert.ok(
    artifact.mtimeMs >= newestSource.mtimeMs,
    [
      'node binding native artifact is older than native source',
      `artifact: ${path.relative(repoRoot, artifact.file)} ${new Date(artifact.mtimeMs).toISOString()}`,
      `source: ${path.relative(repoRoot, newestSource.file)} ${new Date(newestSource.mtimeMs).toISOString()}`,
      'run npm --prefix bindings/node run rebuild-native before framework smoke tests'
    ].join('\n')
  );
});

function activeNativeArtifact() {
  const candidates = [
    path.join(bindingRoot, 'build', 'Release', 'zlink.node'),
    path.join(bindingRoot, 'prebuilds', `${process.platform}-${process.arch}`, 'zlink.node')
  ];
  for (const file of candidates) {
    if (fs.existsSync(file)) {
      return statFile(file);
    }
  }
  return null;
}

function newestNativeSource() {
  const roots = [
    path.join(bindingRoot, 'native', 'src')
  ];
  const files = [path.join(bindingRoot, 'binding.gyp')];
  for (const root of roots) {
    collectFiles(root, files);
  }

  return files
    .filter((file) => fs.existsSync(file))
    .map(statFile)
    .sort((left, right) => right.mtimeMs - left.mtimeMs)[0] ?? null;
}

function collectFiles(root, files) {
  if (!fs.existsSync(root)) {
    return;
  }
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const absolute = path.join(root, entry.name);
    if (entry.isDirectory()) {
      collectFiles(absolute, files);
      continue;
    }
    if (entry.isFile()) {
      files.push(absolute);
    }
  }
}

function statFile(file) {
  return { file, mtimeMs: fs.statSync(file).mtimeMs };
}
