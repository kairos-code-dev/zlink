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

function markdownLinks(content) {
  return [...content.matchAll(/\[[^\]]+\]\(([^)]+)\)/g)]
    .map((match) => match[1])
    .filter((link) => link.length > 0);
}

function isExternalOrRoot(link) {
  return /^(?:https?:|mailto:|#|\/)/.test(link);
}
