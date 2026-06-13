const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const specPath = path.join(workspaceRoot, 'doc', 'spec', 'handler-interfaces.ko.md');
const declarationsRoot = path.join(workspaceRoot, 'packages', 'framework', 'dist', 'contracts');

test('framework contract declarations cover handler interface catalog exports', () => {
  const spec = fs.readFileSync(specPath, 'utf8');
  const declarations = readTree(declarationsRoot);
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

test('framework public root does not expose direct runtime start hosts', () => {
  const framework = require('../../packages/framework/dist');
  const hiddenNames = [
    'ZLinkFrameworkRuntimeHost',
    'ZLinkRegistryRuntime',
    'ZLinkStreamBindingRuntime'
  ];

  const exposed = hiddenNames.filter((name) => name in framework);

  assert.deepEqual(exposed, []);
});

test('framework package exports only the public root contract', () => {
  const packageJson = JSON.parse(
    fs.readFileSync(path.join(workspaceRoot, 'packages', 'framework', 'package.json'), 'utf8'));

  assert.deepEqual(Object.keys(packageJson.exports).sort(), ['.']);
  assert.equal(packageJson.exports['.'].default, './dist/index.js');
  assert.equal(packageJson.exports['.'].types, './dist/index.d.ts');
});

test('spot actor lifecycle handler registration API is not public', () => {
  const declarations = readTree(declarationsRoot);
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
    'ZLinkSpotActorLeftHandler',
    '.handlers.addActorPacket(',
    'addActorPacket(handlerType'
  ];

  const remaining = removedNames.filter((name) => workspaceText.includes(name));

  assert.deepEqual(remaining, []);
});

test('entry spot public surface exposes create lifecycle but no spot create or admission callbacks', () => {
  const declarations = readTree(declarationsRoot);
  const entrySpot = declarationBody(declarations, 'ZLinkEntrySpot');
  const entryContext = declarationBody(declarations, 'ZLinkEntrySpotContext');

  assert.equal(entrySpot.includes('extends ZLinkSpot'), false);
  assert.equal(entrySpot.includes('onCreate?'), false);
  assert.equal(entrySpot.includes('onActorJoin'), false);
  assert.equal(entrySpot.includes('onCreateActor'), true);
  assert.equal(entrySpot.includes('onJoinActor'), true);
  assert.equal(entrySpot.includes('onLeaveActor'), true);
  assert.equal(entryContext.includes('leaveActor'), false);
  assert.equal(entryContext.includes('destroyActor'), true);
  assert.equal(declarationBody(declarations, 'ZLinkSpotContext').includes('destroyActor'), false);
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
  const match = text.match(new RegExp(`export interface ${name}(?:<[^>{]+>)?(?: [^{]+)? \\{([\\s\\S]*?)\\n\\}`));
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
