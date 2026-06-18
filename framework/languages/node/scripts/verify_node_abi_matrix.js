#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');

const nodeRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(nodeRoot, '..', '..', '..');
const nodeDocRoot = path.join(repoRoot, 'framework', 'doc', 'framework', 'node');

const requiredPlatforms = [
  'win-x64',
  'win-arm64',
  'linux-x64',
  'linux-arm64',
  'darwin-x64',
  'darwin-arm64'
];
const requiredNodeVersions = ['20', '22'];

const workflowPath = path.join(repoRoot, '.github', 'workflows', 'framework-node.yml');
const packagePath = path.join(nodeRoot, 'package.json');
const regressionMatrixPath = path.join(nodeDocRoot, 'internals', 'regression-test-matrix.ko.md');
const implementationScopePath = path.join(nodeDocRoot, 'internals', 'implementation-scope-and-nongoals.ko.md');

const errors = [];

const workflow = read(workflowPath);
const packageJson = JSON.parse(read(packagePath));
const regressionMatrix = read(regressionMatrixPath);
const implementationScope = read(implementationScopePath);

for (const platform of requiredPlatforms) {
  requireText(workflow, platform, `${path.relative(repoRoot, workflowPath)} must include ${platform}`);
  requireText(regressionMatrix, platform, `${path.relative(repoRoot, regressionMatrixPath)} must include ${platform}`);
  requireText(implementationScope, platform, `${path.relative(repoRoot, implementationScopePath)} must include ${platform}`);
}

for (const version of requiredNodeVersions) {
  requireWorkflowNodeVersion(workflow, version);
}

requireText(workflow, 'npm --prefix framework/languages/node run verify:p0', 'framework-node workflow must run verify:p0');
requireText(workflow, 'npm --prefix framework/languages/node run verify:cross-language', 'framework-node workflow must run verify:cross-language');
requireScript('verify:p0');
requireScript('verify:samples');
requireScript('verify:runtime-matrix');
requireScript('verify:cross-language');
requireScript('verify:abi-matrix');
requireScript('verify:release');
requireScriptText('verify:release', 'npm run verify:abi-matrix');
requireScriptText('verify:release', 'npm run verify:p0');
requireScriptText('verify:release', 'npm run verify:samples');
requireScriptText('verify:release', 'npm run verify:runtime-matrix');
requireScriptText('verify:release', 'npm run verify:cross-language');

if (errors.length > 0) {
  for (const error of errors) {
    console.error(error);
  }
  process.exit(1);
}

console.log('Node framework ABI matrix gate is declared and linked.');

function read(file) {
  return fs.readFileSync(file, 'utf8');
}

function requireText(text, needle, message) {
  if (!text.includes(needle)) {
    errors.push(message);
  }
}

function requireWorkflowNodeVersion(workflowText, version) {
  const pattern = new RegExp(`(?:^|\\n)\\s*-\\s*${version}\\s*(?:\\n|$)`);
  if (!pattern.test(workflowText)) {
    errors.push(`.github/workflows/framework-node.yml must include Node ${version}`);
  }
}

function requireScript(name) {
  if (typeof packageJson.scripts?.[name] !== 'string') {
    errors.push(`framework/languages/node/package.json must define ${name}`);
  }
}

function requireScriptText(name, text) {
  const script = packageJson.scripts?.[name];
  if (typeof script !== 'string' || !script.includes(text)) {
    errors.push(`framework/languages/node/package.json ${name} must include ${text}`);
  }
}
