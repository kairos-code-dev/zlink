#!/usr/bin/env node

// SPDX-License-Identifier: MPL-2.0

import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const ledgerPath = path.join(root, 'framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md');
const stableJson = value => `${JSON.stringify(value, null, 2)}\n`;
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');

function git(args, encoding = 'utf8') {
  const result = spawnSync('git', args, {cwd: root, encoding});
  if (result.status !== 0) throw new Error(`git ${args.join(' ')} failed: ${String(result.stderr).trim()}`);
  return result.stdout;
}

function currentHash(relative) {
  const index = git(['ls-files', '-s', '-z', '--', relative], null).toString('utf8');
  const recordsByPath = new Map(index.split('\0').filter(Boolean).map(record => {
    const match = /^(\d+) ([0-9a-f]+) \d+\t(.+)$/u.exec(record);
    if (!match) throw new Error(`unexpected git ls-files record: ${record}`);
    return [match[3], {path: match[3], blob: match[2]}];
  }));
  const changed = git(['diff', '--name-only', '-z', '--', relative], null).toString('utf8')
    .split('\0').filter(Boolean);
  const untracked = git(['ls-files', '--others', '--exclude-standard', '-z', '--', relative], null)
    .toString('utf8').split('\0').filter(Boolean);
  for (const file of [...changed, ...untracked]) {
    const absolute = path.join(root, file);
    if (!fs.existsSync(absolute) || !fs.statSync(absolute).isFile()) {
      recordsByPath.delete(file);
      continue;
    }
    const object = git(['hash-object', `--path=${file}`, file]).trim();
    recordsByPath.set(file, {path: file, blob: object});
  }
  const records = [...recordsByPath.values()]
    .sort((left, right) => left.path.localeCompare(right.path, 'en'));
  return records.length === 0 ? null : sha256(stableJson(records));
}

function revisionHash(revision, relative) {
  const output = git(['ls-tree', '-r', '--full-tree', revision, '--', relative]);
  const records = output.trim().length === 0 ? [] : output.trim().split('\n').map(record => {
    const match = /^(\d+) blob ([0-9a-f]+)\t(.+)$/u.exec(record);
    if (!match) throw new Error(`unexpected git ls-tree record: ${record}`);
    return {path: match[3], blob: match[2]};
  }).sort((left, right) => left.path.localeCompare(right.path, 'en'));
  return records.length === 0 ? null : sha256(stableJson(records));
}

function validateManifest(manifest, mode, {checkFiles = true} = {}) {
  const errors = [];
  const fail = message => errors.push(message);
  if (manifest?.schemaVersion !== 1 || manifest?.version !== '11.0.0') {
    fail('manifest schemaVersion/version must identify RouteMesh 11.0.0 schema 1');
  }
  if (manifest?.baselineRevision !== '1f5b979675c4ece4bd9e126d1a66653157ac3b52') {
    fail('manifest baselineRevision must identify the reviewed M5 amendment base');
  }
  if (!['quarantine', 'finalized'].includes(mode)) fail(`unknown mode ${mode}`);
  if (manifest?.execution?.executed !== 0 || manifest?.execution?.skipped !== 0) {
    fail('impact manifest execution counters must remain executed=0 and skipped=0 before activation');
  }
  if (!Array.isArray(manifest?.entries) || manifest.entries.length === 0) {
    fail('manifest entries must be a non-empty array');
    return errors;
  }
  const ledger = fs.readFileSync(ledgerPath, 'utf8');
  const ids = new Set();
  const allowedDispositions = new Set(['retain', 'amend', 'replace', 'add', 'remove']);
  const allowedQuarantine = new Set([
    'pending-disabled-by-contract-amendment', 'active-regression', 'planned-regression',
  ]);
  for (const entry of manifest.entries) {
    const label = entry?.id ?? '<missing-id>';
    if (typeof entry?.id !== 'string' || entry.id.length === 0 || ids.has(entry.id)) {
      fail(`${label}: stable ID is missing or duplicated`);
    }
    ids.add(entry?.id);
    if (!allowedDispositions.has(entry?.disposition)) fail(`${label}: invalid disposition`);
    if (!allowedQuarantine.has(entry?.quarantineStatus)) fail(`${label}: invalid quarantineStatus`);
    for (const field of ['kind', 'language', 'acceptanceIntent', 'specOwner', 'runtimeOwner', 'activationStage']) {
      if (typeof entry?.[field] !== 'string' || entry[field].trim().length === 0) {
        fail(`${label}: ${field} must be a non-empty string`);
      }
    }
    if (!ledger.includes(`\`${entry.runtimeOwner}\``)) fail(`${label}: runtimeOwner is not a ledger ID`);
    const ownerPath = path.join(root, entry.specOwner ?? '');
    if (!fs.existsSync(ownerPath) || !fs.statSync(ownerPath).isFile()) fail(`${label}: specOwner does not exist`);
    if (!Array.isArray(entry.replacementCoverage)) fail(`${label}: replacementCoverage must be an array`);
    if (entry.disposition === 'remove' && entry.replacementCoverage.length === 0) {
      fail(`${label}: remove requires replacement coverage`);
    }
    if (entry.disposition === 'add') {
      if (entry.baselineHash !== null) fail(`${label}: add must not have a baselineHash`);
    } else if (!/^[0-9a-f]{64}$/u.test(entry.baselineHash ?? '')) {
      fail(`${label}: baselineHash must be SHA-256`);
    }
    if (entry.baselinePath !== undefined) {
      if (typeof entry.baselinePath !== 'string' || entry.baselinePath.length === 0) {
        fail(`${label}: baselinePath must be a non-empty repository-relative path`);
      } else if (revisionHash(manifest.baselineRevision, entry.baselinePath) !== entry.baselineHash) {
        fail(`${label}: baselinePath hash does not match baselineHash`);
      }
    }

    if (mode === 'quarantine') {
      if (entry.quarantineStatus === 'pending-disabled-by-contract-amendment'
          && entry.approvedHash !== null) {
        fail(`${label}: quarantined E2E/sample item must not have an approvedHash before finalization`);
      }
    } else if (['amend', 'replace', 'add'].includes(entry.disposition)
        && !/^[0-9a-f]{64}$/u.test(entry.approvedHash ?? '')) {
      fail(`${label}: finalized changed item requires approvedHash`);
    }

    if (!checkFiles || entry.path === null) {
      if (mode === 'finalized' && entry.disposition !== 'remove') fail(`${label}: finalized item requires a path`);
      continue;
    }
    if (typeof entry.path !== 'string' || entry.path.startsWith('/') || entry.path.includes('..')) {
      fail(`${label}: path must be repository-relative`);
      continue;
    }
    const hash = currentHash(entry.path);
    if (hash === null) {
      if (entry.disposition !== 'remove') fail(`${label}: current path is missing`);
      continue;
    }
    if (mode === 'quarantine' && entry.quarantineStatus !== 'pending-disabled-by-contract-amendment') {
      continue;
    }
    const expected = mode === 'finalized' && entry.approvedHash ? entry.approvedHash : entry.baselineHash;
    if (expected && hash !== expected) {
      fail(`${label}: current hash differs from ${mode} approved baseline expected=${expected} actual=${hash}`);
    }
  }
  const publicMemberLanguages = new Set(manifest.entries
    .filter(entry => entry.kind === 'public-member')
    .map(entry => entry.language));
  for (const language of ['cpp', 'dotnet', 'java', 'kotlin', 'node']) {
    if (!publicMemberLanguages.has(language)) {
      fail(`public-member impact coverage is missing for ${language}`);
    }
  }
  const individualRegressionLanguages = new Set(manifest.entries
    .filter(entry => entry.kind === 'regression-test')
    .map(entry => entry.language));
  for (const language of ['cpp', 'dotnet', 'java', 'kotlin', 'node', 'common']) {
    if (!individualRegressionLanguages.has(language)) {
      fail(`individual regression-test inventory is missing for ${language}`);
    }
  }
  const plannedRuntimeLanguages = new Set(manifest.entries
    .filter(entry => entry.kind === 'regression' && entry.disposition === 'add')
    .map(entry => entry.language));
  for (const language of ['cpp', 'dotnet', 'java', 'node']) {
    if (!plannedRuntimeLanguages.has(language)) {
      fail(`planned deterministic runtime regression coverage is missing for ${language}`);
    }
  }
  return errors;
}

function selfTest(manifest) {
  const mutations = [
    candidate => { candidate.entries[1].id = candidate.entries[0].id; },
    candidate => { candidate.entries[0].disposition = 'ignore'; },
    candidate => { candidate.execution.executed = 1; },
    candidate => { candidate.entries[0].baselineHash = 'not-a-sha256'; },
    candidate => {
      candidate.entries[0].disposition = 'remove';
      candidate.entries[0].replacementCoverage = [];
    },
  ];
  for (const mutate of mutations) {
    const candidate = structuredClone(manifest);
    mutate(candidate);
    if (validateManifest(candidate, 'quarantine', {checkFiles: false}).length === 0) {
      throw new Error('impact verifier negative self-test accepted an invalid mutation');
    }
  }
  const hashCandidate = structuredClone(manifest);
  const hashedEntry = hashCandidate.entries.find(entry =>
    entry.path !== null && entry.disposition !== 'add'
      && entry.quarantineStatus === 'pending-disabled-by-contract-amendment');
  if (!hashedEntry) throw new Error('impact verifier self-test requires one quarantined hashed entry');
  hashedEntry.baselineHash = '0'.repeat(64);
  if (validateManifest(hashCandidate, 'quarantine').length === 0) {
    throw new Error('impact verifier negative self-test accepted a current-content hash mismatch');
  }
  return mutations.length + 1;
}

const args = process.argv.slice(2);
const findArg = name => {
  const index = args.indexOf(name);
  return index < 0 ? null : args[index + 1];
};
const manifestArgument = findArg('--manifest');
if (!manifestArgument) {
  process.stderr.write('usage: verify-contract-amendment-impact.mjs --manifest <path> --mode <quarantine|finalized> [--evidence <path>] [--self-test]\n');
  process.exit(2);
}
const manifestFile = path.resolve(root, manifestArgument);
const mode = findArg('--mode') ?? 'quarantine';
const evidenceArgument = findArg('--evidence');
const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
const errors = validateManifest(manifest, mode);
let negativeMutations = 0;
if (args.includes('--self-test')) negativeMutations = selfTest(manifest);
const counts = manifest.entries.reduce((result, entry) => {
  result.byKind[entry.kind] = (result.byKind[entry.kind] ?? 0) + 1;
  result.byDisposition[entry.disposition] = (result.byDisposition[entry.disposition] ?? 0) + 1;
  result.byQuarantineStatus[entry.quarantineStatus] =
    (result.byQuarantineStatus[entry.quarantineStatus] ?? 0) + 1;
  return result;
}, {byKind: {}, byDisposition: {}, byQuarantineStatus: {}});
const result = {
  schemaVersion: 1,
  stage: 'V11-CA-IMPACT',
  mode,
  status: errors.length === 0 ? 'passed' : 'failed',
  manifest: path.relative(root, manifestFile),
  entries: manifest.entries.length,
  ...counts,
  executed: manifest.execution.executed,
  skipped: manifest.execution.skipped,
  negativeMutations,
  errors,
};
if (evidenceArgument) {
  const evidenceFile = path.resolve(evidenceArgument);
  fs.mkdirSync(path.dirname(evidenceFile), {recursive: true});
  fs.writeFileSync(evidenceFile, stableJson(result));
}
if (errors.length > 0) {
  process.stderr.write(`${errors.join('\n')}\n`);
  process.exit(1);
}
process.stdout.write(
  `contract amendment impact ${mode} passed: entries=${manifest.entries.length}`
  + ` pending=${counts.byQuarantineStatus['pending-disabled-by-contract-amendment'] ?? 0}`
  + ` executed=${manifest.execution.executed} skipped=${manifest.execution.skipped}`
  + ` negative_mutations=${negativeMutations}\n`,
);
