import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../..');
const excluded = new Set(['dist', 'log', 'logs', 'node_modules']);

function sourceFiles(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    if (excluded.has(entry.name)) {
      return [];
    }
    const target = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      return sourceFiles(target);
    }
    return /\.(?:cjs|js|mjs|ts)$/.test(entry.name) ? [target] : [];
  });
}

test('Node e2e application code receives configuration only through files and arguments', () => {
  const offenders = sourceFiles(path.join(root, 'e2e'))
    .filter((file) => /\bprocess\.env\b/.test(fs.readFileSync(file, 'utf8')))
    .map((file) => path.relative(root, file));

  assert.deepEqual(offenders, []);
});
