const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const workspaceRoot = path.resolve(__dirname, '..', '..');
const docRoot = path.join(workspaceRoot, 'doc');

const guideFiles = [
  '01-overview.ko.md',
  '02-getting-started.ko.md',
  '03-concepts.ko.md',
  '04-feature-map.ko.md',
  '05-channel-messaging.ko.md',
  '06-spot.ko.md',
  '07-actor-session.ko.md',
  '08-stream.ko.md',
  '09-registry.ko.md',
  '10-monitoring.ko.md',
  '11-interface-catalog.ko.md',
  '12-cross-language.ko.md'
];

test('node guide exposes the 12 required guide chapters', () => {
  const missing = [];
  for (const file of guideFiles) {
    const guidePath = path.join(docRoot, 'guide', file);
    if (!fs.existsSync(guidePath)) {
      missing.push(file);
      continue;
    }
    const content = fs.readFileSync(guidePath, 'utf8');
    if (!/^# /m.test(content) || !/^## 회귀 테스트/m.test(content)) {
      missing.push(file);
    }
  }

  assert.deepEqual(missing, []);
});

test('node README links every guide chapter', () => {
  const readme = fs.readFileSync(path.join(docRoot, 'README.ko.md'), 'utf8');
  const missing = guideFiles.filter((file) => !readme.includes(`./guide/${file}`));

  assert.deepEqual(missing, []);
});

test('node documentation relative markdown links resolve', () => {
  const markdownFiles = allMarkdownFiles(docRoot);
  const broken = [];

  for (const file of markdownFiles) {
    const content = fs.readFileSync(file, 'utf8');
    for (const link of markdownLinks(content)) {
      if (isExternalOrRoot(link)) {
        continue;
      }
      const target = path.resolve(path.dirname(file), link.split('#')[0]);
      if (!fs.existsSync(target)) {
        broken.push(`${path.relative(workspaceRoot, file)} -> ${link}`);
      }
    }
  }

  assert.deepEqual(broken.sort(), []);
});

test('node spec and internals documentation do not depend on legacy draft links', () => {
  const checkedRoots = [
    path.join(docRoot, 'spec'),
    path.join(docRoot, 'internals')
  ];
  const offenders = [];

  for (const root of checkedRoots) {
    for (const file of allMarkdownFiles(root)) {
      const content = fs.readFileSync(file, 'utf8');
      for (const link of markdownLinks(content)) {
        if (link.includes('../draft/') || link.includes('/draft/')) {
          offenders.push(`${path.relative(workspaceRoot, file)} -> ${link}`);
        }
      }
    }
  }

  assert.deepEqual(offenders.sort(), []);
});

test('node implementation reference docs declare regression coverage sections', () => {
  const required = [
    path.join(docRoot, 'README.ko.md'),
    path.join(docRoot, 'IMPLEMENTATION-PLAN.ko.md'),
    path.join(docRoot, 'sample-implementation-plan.ko.md'),
    ...allMarkdownFiles(path.join(docRoot, 'spec')),
    ...allMarkdownFiles(path.join(docRoot, 'internals'))
  ];
  const missing = [];

  for (const file of required) {
    const content = fs.readFileSync(file, 'utf8');
    if (!/^## .*회귀 테스트/m.test(content)) {
      missing.push(path.relative(workspaceRoot, file));
    }
  }

  assert.deepEqual(missing.sort(), []);
});

test('node documentation keeps fanout and route client public surface aligned with contracts', () => {
  const files = [
    path.join(docRoot, 'spec', 'nestjs-channel-messaging.ko.md'),
    path.join(docRoot, 'spec', 'handler-interfaces.ko.md'),
    path.join(docRoot, 'internals', 'di-capability-exposure-policy.ko.md')
  ];
  const offenders = [];

  for (const file of files) {
    const content = fs.readFileSync(file, 'utf8');
    const relative = path.relative(workspaceRoot, file);
    if (/ZLinkFanoutClient\.publish\(channelName/.test(content)) {
      offenders.push(`${relative}: old fanout publish signature`);
    }
    if (/publisher\.publish\(ch, topic, evt\)/.test(content)) {
      offenders.push(`${relative}: old fanout publish example`);
    }
    if (/public client 로 노출하지 않는다|internal-only/.test(content)) {
      offenders.push(`${relative}: route client described as internal-only`);
    }
  }

  assert.deepEqual(offenders.sort(), []);
});

test('node interface catalog names resolve in public package declarations', () => {
  const frameworkDeclarations = packageDeclarations('framework');
  const nestjsDeclarations = packageDeclarations('nestjs');
  const connectorDeclarations = packageDeclarations('stream-connector');
  const missing = [];

  for (const name of [
    'ZLinkChannelClient',
    'ZLinkFanoutClient',
    'ZLinkSendCall',
    'ZLinkRequestCall',
    'ZLinkPublishCall',
    'ZLinkSpot',
    'ZLinkSpotContext',
    'ZLinkSpotManager',
    'ZLinkActor',
    'ZLinkActorContext',
    'ZLinkBoundSession',
    'ZLinkSession',
    'ZLinkSessionContext',
    'ZLinkSessionClient'
  ]) {
    if (!declarationHasSymbol(frameworkDeclarations, name)) {
      missing.push(`@zlink-systems/framework:${name}`);
    }
  }

  for (const name of [
    'ZLinkModule',
    'ZLinkRegistryModule',
    'ZLinkRegistryQueryClientModule',
    'ZLINK_CHANNEL_CLIENT',
    'ZLINK_FANOUT_CLIENT',
    'ZLINK_SPOT_MANAGER',
    'ZLINK_ACTOR_MANAGER',
    'ZLINK_REGISTRY_QUERY'
  ]) {
    if (!declarationHasSymbol(nestjsDeclarations, name)) {
      missing.push(`@zlink-systems/nestjs:${name}`);
    }
  }

  for (const name of [
    'ZlinkStreamConnector',
    'ZlinkStreamHeaderCodec',
    'ZlinkStreamFrameCodec'
  ]) {
    if (!declarationHasSymbol(connectorDeclarations, name)) {
      missing.push(`@zlink-systems/stream-connector:${name}`);
    }
  }

  assert.deepEqual(missing, []);
});

function allMarkdownFiles(root) {
  const files = [];
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      files.push(...allMarkdownFiles(fullPath));
    } else if (entry.isFile() && entry.name.endsWith('.md')) {
      files.push(fullPath);
    }
  }
  return files;
}

function packageDeclarations(packageName) {
  const distRoot = path.join(workspaceRoot, 'packages', packageName, 'dist');
  return allDeclarationFiles(distRoot)
    .map((file) => fs.readFileSync(file, 'utf8'))
    .join('\n');
}

function allDeclarationFiles(root) {
  const files = [];
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const fullPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      files.push(...allDeclarationFiles(fullPath));
    } else if (entry.isFile() && entry.name.endsWith('.d.ts')) {
      files.push(fullPath);
    }
  }
  return files;
}

function declarationHasSymbol(declarations, name) {
  return new RegExp(`\\b(?:export\\s+)?(?:declare\\s+)?(?:interface|class|const|type|function)\\s+${name}\\b`).test(declarations);
}

function markdownLinks(content) {
  return [...content.matchAll(/\[[^\]]+\]\(([^)]+)\)/g)]
    .map((match) => match[1])
    .filter((link) => link.length > 0);
}

function isExternalOrRoot(link) {
  return /^(?:https?:|mailto:|#|\/)/.test(link);
}
