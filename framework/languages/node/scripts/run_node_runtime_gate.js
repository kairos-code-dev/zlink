#!/usr/bin/env node
const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const { ensureNodeBindingDist } = require('./ensure_node_binding_dist');

const nodeRoot = path.resolve(__dirname, '..');
const expectedMajor = Number(process.env.ZLINK_EXPECT_NODE_MAJOR ?? '0');
const actualMajor = Number(process.versions.node.split('.')[0]);
const skippedTestFiles = new Set(
  (process.env.ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS ?? '')
    .split(',')
    .map((value) => value.trim())
    .filter((value) => value.length > 0)
);

if (expectedMajor !== 0 && actualMajor !== expectedMajor) {
  console.error(`Expected Node ${expectedMajor}, got ${process.version}.`);
  process.exit(1);
}

ensureNodeBindingDist();
run(process.execPath, [
  path.resolve(nodeRoot, '../../../bindings/node/node_modules/typescript/bin/tsc'),
  '-b',
  'tsconfig.build.json'
]);
run(process.execPath, [
  path.resolve(nodeRoot, '../../../bindings/node/node_modules/typescript/bin/tsc'),
  '-p',
  'tsconfig.json',
  '--noEmit'
]);
run(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['run', 'lint']);
for (const testFile of listTestFiles(path.join(nodeRoot, 'test'))) {
  const relative = path.relative(nodeRoot, testFile);
  if (skippedTestFiles.has(relative) || skippedTestFiles.has(path.basename(testFile))) {
    console.log(`-- ${relative} # SKIP framework CI excludes e2e sample/runtime checks`);
    continue;
  }
  console.log(`-- ${relative}`);
  run(process.execPath, ['--test', testFile]);
}

function run(command, args) {
  const result = childProcess.spawnSync(command, args, {
    cwd: nodeRoot,
    stdio: 'inherit',
    env: process.env
  });
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function listTestFiles(root) {
  const files = [];
  visit(root);
  return files.sort();

  function visit(current) {
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const fullPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        visit(fullPath);
      } else if (entry.isFile() && entry.name.endsWith('.test.js')) {
        files.push(fullPath);
      }
    }
  }
}
