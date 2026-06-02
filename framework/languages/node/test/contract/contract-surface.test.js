const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const specPath = path.join(workspaceRoot, 'doc', 'spec', 'handler-interfaces.ko.md');
const declarationsPath = path.join(workspaceRoot, 'packages', 'framework', 'dist', 'contracts', 'index.d.ts');

test('framework contract declarations cover handler interface catalog exports', () => {
  const spec = fs.readFileSync(specPath, 'utf8');
  const declarations = fs.readFileSync(declarationsPath, 'utf8');
  const missing = [];

  for (const name of exportedCatalogNames(spec)) {
    const declarationPattern = new RegExp(`\\b(?:interface|type|enum|function)\\s+${name}\\b`);
    if (!declarationPattern.test(declarations)) {
      missing.push(name);
    }
  }

  assert.deepEqual(missing.sort(), []);
});

test('framework runtime exports decorator factories and enums from the catalog', () => {
  const framework = require('../../packages/framework/dist');
  const spec = fs.readFileSync(specPath, 'utf8');
  const missing = [];

  for (const name of runtimeCatalogNames(spec)) {
    if (!(name in framework)) {
      missing.push(name);
    }
  }

  assert.deepEqual(missing.sort(), []);
});

function exportedCatalogNames(spec) {
  return uniqueMatches(spec, /^export\s+(?:interface|type|enum|function)\s+([A-Za-z][A-Za-z0-9_]*)/gm);
}

function runtimeCatalogNames(spec) {
  return uniqueMatches(spec, /^export\s+(?:enum|function)\s+([A-Za-z][A-Za-z0-9_]*)/gm);
}

function uniqueMatches(text, pattern) {
  return [...new Set([...text.matchAll(pattern)].map((match) => match[1]))].sort();
}
