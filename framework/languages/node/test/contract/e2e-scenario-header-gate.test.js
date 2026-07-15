import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../..');
const scenarioIdPattern = /\b[A-Z]{2,3}-[A-Z][0-9]+\b/g;

test('every Node e2e scenario starts with its verification intent', () => {
  const files = scenarioFiles(path.join(root, 'e2e'));
  const canonicalTitles = scenarioTitles(path.resolve(root, '../../doc/framework/common/e2e'));
  assert.equal(files.length, 139);

  for (const file of files) {
    const source = fs.readFileSync(file, 'utf8');
    const ids = [...new Set(source.match(scenarioIdPattern) ?? [])];
    assert.equal(ids.length, 1, `${file} must contain exactly one scenario id`);

    const firstLine = source.split(/\r?\n/, 1)[0];
    assert.match(firstLine, /^\/\/ [A-Z]{2,3}-[A-Z][0-9]+: .+\.$/, `${file} header`);
    assert.ok(firstLine.startsWith(`// ${ids[0]}: `), `${file} header must name ${ids[0]}`);
    const canonicalTitle = canonicalTitles.get(ids[0]);
    if (canonicalTitle !== undefined) {
      assert.equal(firstLine, `// ${ids[0]}: ${canonicalTitle} 시나리오를 검증한다.`);
    }
  }
});

function scenarioFiles(e2eRoot) {
  const files = [];
  for (const config of fs.readdirSync(e2eRoot, { withFileTypes: true })) {
    if (!config.isDirectory()) continue;
    const directory = path.join(e2eRoot, config.name, 'Client', 'Scenarios');
    if (!fs.existsSync(directory)) continue;
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      if (entry.isFile() && entry.name.endsWith('.ts')) files.push(path.join(directory, entry.name));
    }
  }
  return files.sort();
}

function scenarioTitles(commonE2eRoot) {
  const titles = new Map();
  for (const entry of fs.readdirSync(commonE2eRoot, { withFileTypes: true })) {
    if (!entry.isFile() || !/^config-.*\.ko\.md$/.test(entry.name)) continue;
    const source = fs.readFileSync(path.join(commonE2eRoot, entry.name), 'utf8');
    for (const match of source.matchAll(/^####\s+([A-Z]{2,3}-[A-Z][0-9]+)\s+(.+)$/gm)) {
      if (!titles.has(match[1])) titles.set(match[1], match[2].trim());
    }
  }
  return titles;
}
