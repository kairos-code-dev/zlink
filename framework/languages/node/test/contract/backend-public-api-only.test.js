const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const packageRoot = path.join(workspaceRoot, 'packages');

const forbiddenPatterns = [
  /bindings\/node\/src\//,
  /bindings\\node\\src\\/,
  /@zlink-systems\/zlink\/.+/,
  /zlink\/runtime\/native/,
  /zlink\/runtime\/public_bridge/,
  /requireNative/,
  /nativeHandle\s*\(/
];

test('framework packages only depend on binding public entry points', () => {
  const offenders = [];

  for (const file of sourceFiles(packageRoot)) {
    const content = fs.readFileSync(file, 'utf8');
    for (const pattern of forbiddenPatterns) {
      if (pattern.test(content)) {
        offenders.push(`${path.relative(workspaceRoot, file)} matches ${pattern}`);
      }
    }
  }

  assert.deepEqual(offenders, []);
});

function* sourceFiles(root) {
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const absolute = path.join(root, entry.name);
    if (entry.isDirectory()) {
      if (entry.name === 'dist' || entry.name === 'node_modules') {
        continue;
      }
      yield* sourceFiles(absolute);
      continue;
    }
    if (entry.isFile() && absolute.endsWith('.ts')) {
      yield absolute;
    }
  }
}
