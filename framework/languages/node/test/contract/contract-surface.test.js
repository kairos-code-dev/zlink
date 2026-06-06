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

test('spot actor lifecycle handler registration API is not public', () => {
  const declarations = fs.readFileSync(declarationsPath, 'utf8');
  const workspaceText = [
    declarations,
    readTree(path.join(workspaceRoot, 'samples')),
    readTree(path.join(workspaceRoot, 'doc'))
  ].join('\n');
  const removedNames = [
    'addActorJoin',
    'addPostActorJoined',
    'addActorLeft',
    'SpotActorJoinHandler',
    'PostActorJoinedHandler',
    'ActorLeftHandler',
    'ZLinkSpotActorJoinHandler',
    'ZLinkSpotPostActorJoinedHandler',
    'ZLinkSpotActorLeftHandler'
  ];

  const remaining = removedNames.filter((name) => workspaceText.includes(name));

  assert.deepEqual(remaining, []);
});

test('entry spot public surface exposes no create or admission callbacks', () => {
  const declarations = fs.readFileSync(declarationsPath, 'utf8');
  const entrySpot = declarationBody(declarations, 'ZLinkEntrySpot');
  const entryContext = declarationBody(declarations, 'ZLinkEntrySpotContext');

  assert.equal(entrySpot.includes('extends ZLinkSpot'), false);
  assert.equal(entrySpot.includes('onCreate'), false);
  assert.equal(entrySpot.includes('onActorJoin'), false);
  assert.equal(entrySpot.includes('onPostActorJoined'), true);
  assert.equal(entrySpot.includes('onActorLeft'), true);
  assert.equal(entryContext.includes('leaveActor'), false);
  assert.equal(entryContext.includes('close('), false);
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

function declarationBody(text, name) {
  const match = text.match(new RegExp(`export interface ${name}(?: [^{]+)? \\{([\\s\\S]*?)\\n\\}`));
  assert.ok(match, `missing declaration for ${name}`);
  return match[0];
}

function readTree(root) {
  let text = '';
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      text += readTree(fullPath);
      continue;
    }
    if (/\.(?:ts|js|md)$/.test(entry.name)) {
      text += fs.readFileSync(fullPath, 'utf8');
    }
  }
  return text;
}
