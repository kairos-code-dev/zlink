#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
inventory="$repo_root/framework/doc/contract-inventory/route-mesh-v11-document-contract-inventory.json"
wire_schema="$repo_root/framework/runtime/protocol/service-wire-v1.schema.json"
wire_validator="$repo_root/framework/runtime/protocol/validate-service-wire-schema.mjs"
public_contract_trace_generator="$repo_root/scripts/generate-v11-public-contract-trace.mjs"

node "$wire_validator" --self-test "$wire_schema"
node "$public_contract_trace_generator" --check

node - "$repo_root" "$inventory" <<'NODE'
'use strict';

const fs = require('fs');
const path = require('path');
const root = process.argv[2];
const inventoryPath = process.argv[3];
const {
  codeFenceFailures,
  duplicateCodeBlockFailures,
  duplicateDeclarationOwnerFailures,
  exactInterfaceBlockRole,
  fencedBlocks,
  headedFencedBlocks,
  publicDeclarationNames,
  readExactContract,
  relativeMarkdownLinkFailures,
} = require(path.join(root, 'scripts/lib/framework-contract-documents.cjs'));

const failures = [];
const fail = message => failures.push(message);
let javaKotlinSemanticNegativeTestCount = 0;
let terminationContractNegativeTestCount = 0;
if (!fs.existsSync(inventoryPath)) fail(`missing v11 document inventory: ${inventoryPath}`);
const inventory = fs.existsSync(inventoryPath)
  ? JSON.parse(fs.readFileSync(inventoryPath, 'utf8'))
  : {};
const traceConfigPath = path.join(
  root,
  'framework/doc/contract-inventory/route-mesh-v11-public-contract-trace.config.json');
if (!fs.existsSync(traceConfigPath)) fail(`missing v11 public-contract trace config: ${traceConfigPath}`);
const traceConfig = fs.existsSync(traceConfigPath)
  ? JSON.parse(fs.readFileSync(traceConfigPath, 'utf8'))
  : {};
const traceLanguages = new Map(
  (traceConfig.languages || []).map(language => [language.id, language]));

const expectedLanguages = ['dotnet', 'cpp', 'java', 'kotlin', 'node'];
if (inventory.schema !== 1 || inventory.version !== '11.0.0') {
  fail('v11 document inventory must use schema=1 and version=11.0.0');
}
if (JSON.stringify(inventory.languages) !== JSON.stringify(expectedLanguages)) {
  fail(`v11 document inventory language order differs: expected=${expectedLanguages.join(',')}`);
}
if (!inventory.exact_interfaces || typeof inventory.exact_interfaces !== 'object') {
  fail('v11 document inventory exact_interfaces object is missing');
}
if (!Array.isArray(inventory.forbidden_public_code_patterns)
    || inventory.forbidden_public_code_patterns.length === 0) {
  fail('v11 document inventory forbidden_public_code_patterns must be a non-empty array');
}
if (!Array.isArray(inventory.formal_documents) || inventory.formal_documents.length === 0) {
  fail('v11 document inventory formal_documents must be a non-empty array');
}
for (const key of ['required_repository_file_fragments', 'forbidden_repository_file_fragments']) {
  if (!inventory[key] || typeof inventory[key] !== 'object' || Array.isArray(inventory[key])) {
    fail(`v11 document inventory ${key} must be an object`);
  }
}
if (!Array.isArray(inventory.target_document_sets)
    || inventory.target_document_sets.length !== 2) {
  fail('v11 document inventory must contain target-spec and target-internals document sets');
} else if (JSON.stringify(inventory.target_document_sets.map(set => set.name))
           !== JSON.stringify(['target-spec', 'target-internals'])) {
  fail('v11 target document set order must be target-spec then target-internals');
}
if (!inventory.plan_consolidation || typeof inventory.plan_consolidation !== 'object') {
  fail('v11 document inventory plan_consolidation object is missing');
}
const expectedTerminationMembers = ['drain', 'awaitDrained', 'retire', 'shutdown'];
const expectedTerminationOwners = ['ZLinkFrameworkRuntime', 'ZLinkDrainControl'];
const expectedTopologyOwners = [
  'ZLinkRouteMeshRuntime',
  'ZLinkClientServerRuntime',
  'ZLinkFanoutRuntime',
];
const terminationOwnerPolicy = inventory.java_kotlin_termination_owners || {};
if (JSON.stringify(terminationOwnerPolicy.members)
    !== JSON.stringify(expectedTerminationMembers)
    || JSON.stringify(terminationOwnerPolicy.allowed)
    !== JSON.stringify(expectedTerminationOwners)
    || JSON.stringify(terminationOwnerPolicy.forbidden_topologies)
    !== JSON.stringify(expectedTopologyOwners)) {
  fail('Java/Kotlin termination owner policy differs from the host-only contract');
}

const expectedTerminationResultContract = {
  formal_path: 'framework/doc/framework/spec/server/54-graceful-drain-handoff.ko.md',
  target_path: 'framework/doc/plan/v11.0/target-spec/07-location-maintenance.ko.md',
  e2e_path: 'framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md',
  outcomes: [
    { name: 'Stopped', wire_value: 0, reasons: ['None'] },
    { name: 'Blocked', wire_value: 1, reasons: [
      'TargetUnavailable', 'StoreUnavailable', 'TransferDisabled', 'StateIncompatible',
      'DeadlineExceeded', 'RuntimeNotReady',
    ] },
    { name: 'ForceStopped', wire_value: 2, reasons: [
      'DeadlineExceeded', 'TransferFailed', 'TeardownFailed',
    ] },
  ],
  reasons: [
    { name: 'None', wire_value: 0 },
    { name: 'TargetUnavailable', wire_value: 1 },
    { name: 'StoreUnavailable', wire_value: 2 },
    { name: 'TransferDisabled', wire_value: 3 },
    { name: 'StateIncompatible', wire_value: 4 },
    { name: 'DeadlineExceeded', wire_value: 5 },
    { name: 'TransferFailed', wire_value: 6 },
    { name: 'TeardownFailed', wire_value: 7 },
    { name: 'RuntimeNotReady', wire_value: 8 },
  ],
  checkpoint_ceiling_pair: 'Blocked/StateIncompatible',
  forbidden_fragments: ['CheckpointTooLarge', '| `Completed` |', '| `Failed` |'],
};
const terminationResultContract = inventory.termination_result_contract || {};
if (JSON.stringify(terminationResultContract)
    !== JSON.stringify(expectedTerminationResultContract)) {
  fail('termination result inventory differs from the closed formal contract');
}

const parseTerminationOutcomeTable = source => {
  const lines = source.split(/\r?\n/u);
  const header = lines.findIndex(line => /^\|\s*값\s*\|\s*Outcome\s*\|\s*허용 reason\s*\|/u.test(line));
  if (header < 0) return [];
  const rows = [];
  for (let index = header + 2; index < lines.length && lines[index].startsWith('|'); index += 1) {
    const cells = lines[index].split('|').slice(1, -1).map(cell => cell.trim());
    if (cells.length < 3) continue;
    const name = /^`([^`]+)`$/u.exec(cells[1])?.[1];
    const wireValue = /^(0|[1-9][0-9]*)$/u.test(cells[0]) ? Number(cells[0]) : NaN;
    const reasons = [...cells[2].matchAll(/`([^`]+)`/gu)].map(match => match[1]);
    rows.push({ name, wire_value: wireValue, reasons });
  }
  return rows;
};

const parseTerminationReasonValues = source => {
  const start = source.indexOf('Reason wire 값은');
  if (start < 0) return [];
  const end = source.indexOf('이다.', start);
  if (end < 0) return [];
  return [...source.slice(start, end).matchAll(/`([A-Za-z][A-Za-z0-9]*)=(\d+)`/gu)]
    .map(match => ({ name: match[1], wire_value: Number(match[2]) }));
};

const terminationDocumentFailures = (source, label) => {
  const messages = [];
  if (JSON.stringify(parseTerminationOutcomeTable(source))
      !== JSON.stringify(expectedTerminationResultContract.outcomes)) {
    messages.push(`${label} termination outcome/reason pairs differ`);
  }
  if (JSON.stringify(parseTerminationReasonValues(source))
      !== JSON.stringify(expectedTerminationResultContract.reasons)) {
    messages.push(`${label} termination reason wire values differ`);
  }
  for (const fragment of expectedTerminationResultContract.forbidden_fragments) {
    if (source.includes(fragment)) messages.push(`${label} contains forbidden ${fragment}`);
  }
  return messages;
};

const terminationSources = new Map();
for (const [label, relative] of [
  ['formal', expectedTerminationResultContract.formal_path],
  ['target', expectedTerminationResultContract.target_path],
]) {
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    fail(`termination ${label} document is missing: ${relative}`);
    continue;
  }
  const source = fs.readFileSync(absolute, 'utf8');
  terminationSources.set(label, source);
  for (const message of terminationDocumentFailures(source, label)) fail(message);
}
const terminationE2ePath = path.join(root, expectedTerminationResultContract.e2e_path);
if (!fs.existsSync(terminationE2ePath)) {
  fail(`termination E2E document is missing: ${expectedTerminationResultContract.e2e_path}`);
} else {
  const source = fs.readFileSync(terminationE2ePath, 'utf8');
  if (!source.includes(`\`${expectedTerminationResultContract.checkpoint_ceiling_pair}\``)) {
    fail(`termination E2E checkpoint ceiling must use ${expectedTerminationResultContract.checkpoint_ceiling_pair}`);
  }
  for (const fragment of expectedTerminationResultContract.forbidden_fragments) {
    if (source.includes(fragment)) fail(`termination E2E contains forbidden ${fragment}`);
  }
}

const terminationNegativeFixtures = [
  ['Blocked deadline omitted', 'formal', source => source.replace(', `DeadlineExceeded`, `RuntimeNotReady`', ', `RuntimeNotReady`')],
  ['checkpoint reason widened', 'target', source => source.replace('`StateIncompatible`', '`CheckpointTooLarge`')],
  ['teardown outcome widened', 'target', source => source.replace('| 2 | `ForceStopped` |', '| 2 | `Failed` |')],
];
for (const [fixture, label, mutate] of terminationNegativeFixtures) {
  const source = terminationSources.get(label);
  if (!source) continue;
  const candidate = mutate(source);
  if (candidate === source || terminationDocumentFailures(candidate, `${label}:${fixture}`).length === 0) {
    fail(`termination contract negative self-test did not reject ${fixture}`);
  }
  terminationContractNegativeTestCount += 1;
}

const filesUnder = relativeDirectory => {
  const files = [];
  const visit = current => {
    const absolute = path.join(root, current);
    if (!fs.existsSync(absolute)) return;
    for (const entry of fs.readdirSync(absolute, { withFileTypes: true })) {
      const relative = path.posix.join(current, entry.name);
      if (entry.isDirectory()) visit(relative);
      else if (entry.isFile() || entry.isSymbolicLink()) files.push(relative);
    }
  };
  visit(relativeDirectory);
  return files.sort((left, right) => left.localeCompare(right, 'en'));
};
const markdownDocumentsUnder = relativeDirectory => filesUnder(relativeDirectory)
  .filter(relative => relative.endsWith('.ko.md'));
const maskCodeLiterals = source => {
  const masked = [...source];
  const blank = index => {
    if (masked[index] !== '\n' && masked[index] !== '\r') masked[index] = ' ';
  };
  for (let index = 0; index < source.length;) {
    if (source.startsWith('//', index)) {
      while (index < source.length && source[index] !== '\n') blank(index++);
      continue;
    }
    if (source.startsWith('/*', index)) {
      blank(index++);
      blank(index++);
      while (index < source.length && !source.startsWith('*/', index)) blank(index++);
      if (index < source.length) {
        blank(index++);
        blank(index++);
      }
      continue;
    }
    const quote = source[index];
    const tripleQuote = quote === '"' && source.startsWith('"""', index);
    if (tripleQuote) {
      for (let count = 0; count < 3; count += 1) blank(index++);
      while (index < source.length && !source.startsWith('"""', index)) blank(index++);
      for (let count = 0; count < 3 && index < source.length; count += 1) blank(index++);
      continue;
    }
    if (quote === '"' || quote === '\'' || quote === '`') {
      blank(index++);
      while (index < source.length) {
        if (source[index] === '\\') {
          blank(index++);
          if (index < source.length) blank(index++);
          continue;
        }
        const current = source[index];
        blank(index++);
        if (current === quote) break;
      }
      continue;
    }
    index += 1;
  }
  return masked.join('');
};

const matchingBrace = (masked, open) => {
  let depth = 0;
  for (let index = open; index < masked.length; index += 1) {
    if (masked[index] === '{') depth += 1;
    else if (masked[index] === '}') {
      depth -= 1;
      if (depth === 0) return index;
    }
  }
  return -1;
};

const declarationSpans = (source, tag) => {
  const masked = maskCodeLiterals(source);
  const pattern = tag === 'java'
    ? /\b(?:(?:public|protected|private|sealed|non-sealed|final|abstract|static)\s+)*(class|interface|record|enum|@interface)\s+([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)/gu
    : /\b(?:(?:public|protected|private|internal|sealed|data|enum|value|annotation|open|abstract|final|inner)\s+)*(class|interface|object)\s+([A-Za-z_$][\w$]*)/gu;
  const declarations = [];
  for (const match of masked.matchAll(pattern)) {
    const open = masked.indexOf('{', match.index + match[0].length);
    if (open < 0) continue;
    const close = matchingBrace(masked, open);
    if (close < 0) continue;
    const qualifiedName = match[2];
    declarations.push({
      kind: match[1],
      name: qualifiedName.slice(qualifiedName.lastIndexOf('.') + 1),
      nameEnd: match.index + match[0].length,
      start: match.index,
      open,
      close,
    });
  }
  return declarations;
};

const enclosingDeclaration = (declarations, index) => declarations
  .filter(declaration => declaration.start <= index && index < declaration.close)
  .sort((left, right) => (left.close - left.start) - (right.close - right.start))[0];

const terminationNameExpression = expectedTerminationMembers.join('|');
const terminationDeclarations = (source, tag) => {
  const masked = maskCodeLiterals(source);
  const declarations = declarationSpans(source, tag);
  const members = [];
  if (tag === 'java') {
    const pattern = new RegExp(
      `(?:^|[;{}])[ \\t]*(?!(?:return|throw|new)\\b)`
      + `(?:(?:@[A-Za-z_$][\\w$]*(?:\\([^\\n]*\\))?)[ \\t]+)*`
      + `(?:(?:public|protected|private|abstract|default|static|final|synchronized|native|strictfp)[ \\t]+)*`
      + `(?:[A-Za-z_$][^;={}\\n()]*?[ \\t]+)`
      + `(${terminationNameExpression})[ \\t]*\\(`,
      'gmu');
    for (const match of masked.matchAll(pattern)) {
      const memberIndex = match.index + match[0].lastIndexOf(match[1]);
      members.push({
        name: match[1],
        owner: enclosingDeclaration(declarations, memberIndex)?.name || '<Java top-level>',
        extension: false,
      });
    }
    return members;
  }

  const pattern = new RegExp(
    `^[ \\t]*(?:(?:public|protected|private|internal|override|open|final|abstract|suspend|operator|infix|inline|tailrec|external)[ \\t]+)*`
    + `fun[ \\t]+(?:<[^>\\n]+>[ \\t]*)?`
    + `(?:([A-Za-z_$][\\w$]*(?:\\.[A-Za-z_$][\\w$]*)*(?:<[^>\\n]+>)?\\??)[ \\t]*\\.[ \\t]*)?`
    + `(${terminationNameExpression})[ \\t]*\\(`,
    'gmu');
  for (const match of masked.matchAll(pattern)) {
    const memberIndex = match.index + match[0].lastIndexOf(match[2]);
    const receiver = match[1];
    members.push({
      name: match[2],
      owner: receiver
        ? `<Kotlin extension:${receiver}>`
        : enclosingDeclaration(declarations, memberIndex)?.name || '<Kotlin top-level>',
      extension: receiver !== undefined,
    });
  }
  return members;
};

const terminationOwnerFailures = (entries, language) => {
  const allowed = new Set(expectedTerminationOwners);
  const messages = [];
  for (const entry of entries) {
    for (const block of entry.blocks) {
      if (block.tag !== 'java' && block.tag !== 'kotlin') continue;
      for (const member of terminationDeclarations(block.source, block.tag)) {
        if (!allowed.has(member.owner)) {
          messages.push(
            `${language}: ${entry.relative}: ${member.owner}.${member.name} is not host-owned`);
        }
      }
    }
  }
  return messages;
};

const frameworkSnapshotFailures = source => {
  const masked = maskCodeLiterals(source);
  const records = declarationSpans(source, 'java')
    .filter(declaration => declaration.kind === 'record'
      && declaration.name === 'ZLinkFrameworkRuntimeSnapshot');
  const messages = [];
  if (records.length !== 1) {
    messages.push(`snapshot record declaration count differs: actual=${records.length}`);
    return messages;
  }
  const record = records[0];
  const header = masked.slice(record.nameEnd, record.open);
  const body = masked.slice(record.open + 1, record.close);
  const componentCount = [
    ...header.matchAll(/\bOptional\s*<\s*ZLinkTerminationResult\s*>\s+terminalResult\b/gu),
  ].length;
  const nameCount = [...header.matchAll(/\bterminalResult\b/gu)].length;
  const explicitMemberCount = [...body.matchAll(/\bterminalResult\s*\(/gu)].length;
  if (componentCount !== 1 || nameCount !== 1) {
    messages.push(
      `snapshot terminalResult component count differs: typed=${componentCount} named=${nameCount}`);
  }
  if (explicitMemberCount !== 0) {
    messages.push(
      `snapshot repeats generated terminalResult accessor: actual=${explicitMemberCount}`);
  }
  return messages;
};

let exactDocumentCount = 0;
let exactFenceCount = 0;
let syntaxAndExampleFenceCount = 0;
let packageDeclarationOwnerCount = 0;
let blockRoleNegativeMutationCount = 0;
const exactFenceRoleCounts = new Map([
  ['package-contract', 0],
  ['application-example', 0],
  ['documentation-support', 0],
]);
for (const language of expectedLanguages) {
  const projection = inventory.exact_interfaces?.[language];
  if (!projection) {
    fail(`missing exact-interface projection: ${language}`);
    continue;
  }
  for (const key of ['code_tags', 'required_fragments']) {
    if (!Array.isArray(projection[key]) || projection[key].length === 0) {
      fail(`${language} ${key} must be a non-empty array`);
    }
  }
  for (const key of ['required_file_fragments', 'forbidden_file_fragments']) {
    if (projection[key] !== undefined
        && (!projection[key] || typeof projection[key] !== 'object'
            || Array.isArray(projection[key]))) {
      fail(`${language} ${key} must be an object when present`);
    }
  }
  for (const key of ['minimum_documents', 'minimum_code_blocks']) {
    if (!Number.isInteger(projection[key]) || projection[key] <= 0) {
      fail(`${language} ${key} must be a positive integer`);
    }
  }

  const languageRoleConfig = traceLanguages.get(language);
  if (!languageRoleConfig) {
    fail(`${language} public-contract trace language policy is missing`);
    continue;
  }
  if (JSON.stringify(languageRoleConfig.acceptedCodeTags)
      !== JSON.stringify(projection.code_tags)) {
    fail(`${language} DOC and TRACE syntax tag policies differ`);
  }
  const contract = readExactContract(root, language);
  contract.entries = contract.entries.map(entry => ({
    ...entry,
    blocks: entry.blocks.map(block => ({
      ...block,
      ...exactInterfaceBlockRole(traceConfig, languageRoleConfig, entry.relative, block),
    })),
  }));
  contract.code = contract.entries.flatMap(entry => entry.blocks
    .filter(block => block.role === 'package-contract')
    .map(block => block.source)).join('\n');
  exactDocumentCount += contract.documents.length;
  const blocks = contract.entries.flatMap(entry => entry.blocks);
  const syntaxAndExampleBlocks = blocks
    .filter(block => block.role === 'package-contract' || block.role === 'application-example');
  exactFenceCount += blocks.length;
  syntaxAndExampleFenceCount += syntaxAndExampleBlocks.length;
  for (const block of blocks) {
    if (!exactFenceRoleCounts.has(block.role)) {
      fail(`${language} exact fence has an unknown role: ${block.role}`);
      continue;
    }
    exactFenceRoleCounts.set(block.role, exactFenceRoleCounts.get(block.role) + 1);
  }
  if (contract.documents.length < projection.minimum_documents) {
    fail(`${language} exact document count is too small: expected>=${projection.minimum_documents} actual=${contract.documents.length}`);
  }
  if (!contract.documents.some(relative => relative.endsWith('/README.ko.md'))) {
    fail(`${language} exact interface README is missing`);
  }
  if (syntaxAndExampleBlocks.length < projection.minimum_code_blocks) {
    fail(`${language} syntax/example fence count is too small: expected>=${projection.minimum_code_blocks} actual=${syntaxAndExampleBlocks.length}`);
  }
  for (const fragment of projection.required_fragments || []) {
    if (!contract.source.includes(fragment)) {
      fail(`${language} exact contract is missing semantic projection: ${fragment}`);
    }
  }
  const exactDirectory = path.posix.join(
    'framework/doc/framework/spec/server/languages', language, 'interfaces');
  for (const [name, fragments] of Object.entries(projection.required_file_fragments || {})) {
    if (!Array.isArray(fragments) || fragments.length === 0) {
      fail(`${language} required_file_fragments must contain non-empty arrays: ${name}`);
      continue;
    }
    const relative = path.posix.join(exactDirectory, name);
    const absolute = path.join(root, relative);
    if (!fs.existsSync(absolute)) {
      fail(`${language} exact semantic owner is missing: ${relative}`);
      continue;
    }
    const source = fs.readFileSync(absolute, 'utf8');
    for (const fragment of fragments) {
      if (!source.includes(fragment)) {
        fail(`${language} exact semantic owner is missing ${fragment}: ${relative}`);
      }
    }
  }
  for (const [name, fragments] of Object.entries(projection.forbidden_file_fragments || {})) {
    if (!Array.isArray(fragments) || fragments.length === 0) {
      fail(`${language} forbidden_file_fragments must contain non-empty arrays: ${name}`);
      continue;
    }
    const relative = path.posix.join(exactDirectory, name);
    const absolute = path.join(root, relative);
    if (!fs.existsSync(absolute)) {
      fail(`${language} exact semantic owner is missing: ${relative}`);
      continue;
    }
    const source = fs.readFileSync(absolute, 'utf8');
    for (const fragment of fragments) {
      if (source.includes(fragment)) {
        fail(`${language} exact semantic owner contains forbidden duplicate ${fragment}: ${relative}`);
      }
    }
  }
  for (const expression of inventory.forbidden_public_code_patterns || []) {
    let pattern;
    try {
      pattern = new RegExp(expression, 'u');
    } catch (error) {
      fail(`invalid forbidden public-code pattern ${expression}: ${error.message}`);
      continue;
    }
    if (pattern.test(contract.code)) {
      fail(`${language} exact public declaration contains forbidden v11 surface: ${expression}`);
    }
  }
  for (const message of codeFenceFailures(root, contract.documents)) {
    fail(`exact code fence: ${message}`);
  }
  for (const message of relativeMarkdownLinkFailures(root, contract.documents)) {
    fail(`exact link: ${message}`);
  }
  const syntaxAndExampleContract = {
    ...contract,
    entries: contract.entries.map(entry => ({
      ...entry,
      blocks: entry.blocks.filter(block => block.role !== 'documentation-support'),
    })),
  };
  for (const message of duplicateCodeBlockFailures(language, syntaxAndExampleContract)) {
    fail(`duplicate exact code block: ${message}`);
  }
  const duplicateOwners = duplicateDeclarationOwnerFailures(language, contract);
  for (const message of duplicateOwners) fail(`duplicate exact declaration owner: ${message}`);
  const ownerNames = new Set();
  for (const entry of contract.entries) {
    const code = entry.blocks
      .filter(block => block.role === 'package-contract')
      .map(block => block.source)
      .join('\n');
    for (const name of publicDeclarationNames(language, code)) ownerNames.add(name);
  }
  if (ownerNames.size === 0) fail(`${language} exact public declaration inventory is empty`);
  packageDeclarationOwnerCount += ownerNames.size;
}

const applicationExampleMutationSource = [
  '## Example fixture',
  '',
  '```csharp',
  'public sealed class MustNotBecomePackageOwner {}',
  '```',
].join('\n');
const applicationExampleMutationDocument =
  'framework/doc/framework/spec/server/languages/dotnet/interfaces/99-examples.ko.md';
const dotnetRoleConfig = traceLanguages.get('dotnet');
const applicationExampleMutationBlocks = headedFencedBlocks(applicationExampleMutationSource)
  .map(block => ({
    ...block,
    ...exactInterfaceBlockRole(
      traceConfig,
      dotnetRoleConfig,
      applicationExampleMutationDocument,
      block),
  }));
const applicationExampleMutationOwnerNames = new Set(applicationExampleMutationBlocks
  .filter(block => block.role === 'package-contract')
  .flatMap(block => [...publicDeclarationNames('dotnet', block.source)]));
if (applicationExampleMutationBlocks.length !== 1
    || applicationExampleMutationBlocks[0].role !== 'application-example'
    || applicationExampleMutationOwnerNames.size !== 0) {
  fail('application example declaration entered the package declaration-owner inventory');
} else {
  blockRoleNegativeMutationCount += 1;
}

const javaExactDirectory =
  'framework/doc/framework/spec/server/languages/java/interfaces';
const javaMonitoringRelative = path.posix.join(javaExactDirectory, 'monitoring.ko.md');
const javaMonitoringSource = fs.readFileSync(path.join(root, javaMonitoringRelative), 'utf8');
const javaSourceHeadings = [
  'Host runtime observation exact source signature',
  'Topology runtime observation exact source signature',
];
const javaSourceBlocks = new Map();
for (const heading of javaSourceHeadings) {
  const sourceBlocks = headedFencedBlocks(javaMonitoringSource)
    .filter(block => block.tag === 'java' && block.heading === heading);
  if (sourceBlocks.length !== 1) {
    fail(`Java exact source heading must own one java block: ${heading}`);
    continue;
  }
  const block = sourceBlocks[0];
  javaSourceBlocks.set(heading, block);
  const memberSource = block.source.split(/\r?\n/u)
    .filter(line => !/^\s*(?:package|import)\s+/u.test(line))
    .join('\n');
  const fullyQualifiedType =
    /\b(?:java|javax|jakarta|org|com|io|systems|kotlin|kotlinx)\.[A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)+/u;
  if (fullyQualifiedType.test(memberSource)) {
    fail(`Java exact source block repeats a fully-qualified type: ${javaMonitoringRelative}:${block.startLine}`);
  }
}
const javaHostObservation = javaSourceBlocks.get(
  'Host runtime observation exact source signature');
if (javaHostObservation) {
  for (const message of frameworkSnapshotFailures(javaHostObservation.source)) {
    fail(`Java host runtime snapshot contract: ${javaMonitoringRelative}:${javaHostObservation.startLine}: ${message}`);
  }
}

const javaCommonRelative = path.posix.join(javaExactDirectory, 'common-runtime.ko.md');
const javaCommonSource = fs.readFileSync(path.join(root, javaCommonRelative), 'utf8');
const javaCommonCode = fencedBlocks(javaCommonSource, ['java'])
  .map(block => block.source).join('\n');
const javaRuntimeOwnerPattern =
  /\bpublic\s+final\s+class\s+(?:systems\.zlink\.framework\.runtime\.host\.)?ZLinkFrameworkRuntime\b/gu;
const javaRuntimeOwnerCount = source => [...source.matchAll(javaRuntimeOwnerPattern)].length;
if (javaRuntimeOwnerCount(javaCommonCode) !== 1) {
  fail(`Java runtime must have one canonical exact declaration owner: actual=${javaRuntimeOwnerCount(javaCommonCode)}`);
}
for (const accessor of ['routeMeshRuntime', 'clientServerRuntime', 'fanoutRuntime']) {
  const pattern = new RegExp(`\\b${accessor}\\s*\\(\\s*\\)\\s*;`, 'gu');
  const count = [...javaCommonCode.matchAll(pattern)].length;
  if (count !== 1) {
    fail(`Java runtime topology accessor must have one canonical declaration: ${accessor} actual=${count}`);
  }
}
const javaCommonNormalized = javaCommonCode.replace(/\s+/gu, ' ');
for (const fragment of [
  'implements AutoCloseable, ZLinkMessageFlowControl, ZLinkDrainControl, ZLinkRuntimeQuery',
  'public static final Duration DEFAULT_TERMINATION_DEADLINE = Duration.ofSeconds(30);',
]) {
  if (!javaCommonNormalized.includes(fragment)) {
    fail(`Java canonical runtime declaration is missing: ${fragment}`);
  }
}
if (javaRuntimeOwnerCount(`${javaCommonCode}\npublic final class systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime {}`) !== 2) {
  fail('Java duplicate runtime owner negative self-test fixture is invalid');
} else {
  javaKotlinSemanticNegativeTestCount += 1;
}
for (const [label, pattern] of [
  ['public runtime constructor', /\bpublic\s+ZLinkFrameworkRuntime\s*\(/u],
  ['public runtime factory', /\bpublic\s+static\s+(?:systems\.zlink\.framework\.runtime\.host\.)?ZLinkFrameworkRuntime\s+\w+\s*\(/u],
  ['public runtime bootstrap member', /\bpublic\s+[^;{}\n]+\s+(?:bootstrap|start)\s*\(/u],
]) {
  if (pattern.test(javaCommonCode)) {
    fail(`Java runtime exact surface contains forbidden ${label}`);
  }
}

const javaConfigurationRelative = path.posix.join(
  javaExactDirectory, 'configuration-host.ko.md');
const javaConfigurationSource = fs.readFileSync(
  path.join(root, javaConfigurationRelative), 'utf8');
const javaConfigurationCode = fencedBlocks(javaConfigurationSource, ['java'])
  .map(block => block.source).join('\n');
for (const [label, pattern] of [
  ['runtime.internal type', /systems\.zlink\.framework\.runtime\.internal/u],
  ['auto-configuration implementation', /ZLinkFrameworkAutoConfiguration/u],
  ['framework lifecycle implementation', /ZLinkFrameworkLifecycle/u],
  ['monitoring lifecycle implementation', /ZLinkMonitoringLifecycle/u],
  ['public runtime bean factory', /\bzlinkFrameworkRuntime\s*\(/u],
]) {
  if (pattern.test(javaConfigurationCode)) {
    fail(`Java configuration public contract exposes ${label}`);
  }
}

const javaLifecycleRelative =
  'framework/doc/framework/java/internals/runtime-lifecycle.ko.md';
const javaLifecycleSource = fs.readFileSync(path.join(root, javaLifecycleRelative), 'utf8');
if (!/^\| `ZLinkFrameworkAutoConfigurationTest\.exposesSingleRuntimeAndTopologyRuntimeBeans` \| [^\n]*`assertSame`[^\n]*\|$/mu.test(javaLifecycleSource)) {
  fail('Java singleton topology bean test must compare each facade view by reference identity');
}

const kotlinSourceContract = readExactContract(root, 'kotlin', ['kotlin']);
const kotlinAsFlowDeclarations = [];
const kotlinAsFlowPattern =
  /\b(?:public\s+)?fun\s*(?:<[^>]+>\s*)?[^(){};=]*?\.asFlow\s*\(\s*\)/gu;
for (const entry of kotlinSourceContract.entries) {
  for (const block of entry.blocks) {
    const normalized = block.source.replace(/\s+/gu, ' ').trim();
    for (const ignored of normalized.matchAll(kotlinAsFlowPattern)) {
      kotlinAsFlowDeclarations.push(entry.relative);
    }
  }
}
const kotlinAsFlowOwner =
  'framework/doc/framework/spec/server/languages/kotlin/interfaces/location-maintenance.ko.md';
if (kotlinAsFlowDeclarations.length !== 1
    || kotlinAsFlowDeclarations[0] !== kotlinAsFlowOwner) {
  fail(`Kotlin asFlow source declaration must have one owner: actual=${kotlinAsFlowDeclarations.join(',') || 'none'}`);
}
const kotlinGeneratedContract = readExactContract(root, 'kotlin', ['java']);
const kotlinGeneratedAsFlowOwners = [];
for (const entry of kotlinGeneratedContract.entries) {
  for (const block of entry.blocks) {
    for (const ignored of block.source.matchAll(/\basFlow\s*\(/gu)) {
      kotlinGeneratedAsFlowOwners.push(entry.relative);
    }
  }
}
if (kotlinGeneratedAsFlowOwners.length !== 1
    || kotlinGeneratedAsFlowOwners[0] !== kotlinAsFlowOwner) {
  fail(`Kotlin generated asFlow signature must have one owner: actual=${kotlinGeneratedAsFlowOwners.join(',') || 'none'}`);
}

const javaTerminationContract = readExactContract(root, 'java', ['java']);
const kotlinTerminationContract = readExactContract(root, 'kotlin', ['kotlin', 'java']);
for (const [language, contract] of [
  ['java', javaTerminationContract],
  ['kotlin', kotlinTerminationContract],
]) {
  for (const message of terminationOwnerFailures(contract.entries, language)) {
    fail(`host-only termination owner: ${message}`);
  }
  const terminationPattern = /\b(?:drain|awaitDrained|retire|shutdown)\s*\(([^)]*)\)/gu;
  for (const match of contract.code.matchAll(terminationPattern)) {
    const parameters = match[1];
    if (/\b(?:java\.lang\.)?String\b|\b[Mm]eshName\b|\btarget\b/u.test(parameters)) {
      fail(`${language} host termination member contains a topology target parameter: ${match[0]}`);
    }
  }
}

const cppTerminationContract = readExactContract(root, 'cpp', ['cpp']);
const cppDrainReasonMatch = cppTerminationContract.code.match(
  /enum class drain_force_reason_t\s*\{([\s\S]*?)\};/u);
if (!cppDrainReasonMatch) {
  fail('C++ deprecated drain_force_reason_t declaration is missing');
} else {
  const actualReasons = cppDrainReasonMatch[1]
    .split(',')
    .map(value => value.trim())
    .filter(Boolean);
  const expectedReasons = ['deadline_exceeded', 'transfer_failed', 'teardown_failed'];
  if (JSON.stringify(actualReasons) !== JSON.stringify(expectedReasons)) {
    fail(`C++ deprecated drain reasons are not the closed Shutdown mapping: actual=${actualReasons.join(',')}`);
  }
}
const cppTerminationNormalized = cppTerminationContract.source.replace(/\s+/gu, ' ');
for (const fragment of [
  'Deprecated `drain(deadline)`은 같은 deadline으로 `shutdown()`을 호출한다.',
  '`stopped/none`은 `drained_t`로 변환한다.',
  '`force_stopped/deadline_exceeded`, `force_stopped/transfer_failed`, `force_stopped/teardown_failed`는 각각 같은 이름의 `drain_force_reason_t`',
  '`blocked`는 Shutdown 결과가 아니므로 `drain_result_t`에 추가하지 않는다.',
]) {
  if (!cppTerminationNormalized.includes(fragment)) {
    fail(`C++ deprecated drain facade is missing total Shutdown mapping: ${fragment}`);
  }
}
if (/\b(?:drain_state_publish_failed|owner_cleanup_failed)\b/u.test(cppTerminationContract.code)) {
  fail('C++ deprecated drain facade retains a stale non-isomorphic failure reason');
}

const terminationOwnerNegativeFixtures = [
  {
    label: 'Java RouteMesh parameterless shutdown',
    tag: 'java',
    source: 'public interface ZLinkRouteMeshRuntime { void shutdown(); }',
  },
  {
    label: 'Kotlin topology extension shutdown',
    tag: 'kotlin',
    source: 'fun ZLinkRouteMeshRuntime.shutdown(): Unit = Unit',
  },
  {
    label: 'generated Kotlin extension retire',
    tag: 'java',
    source: 'public final class ZLinkRuntimeExtensionsKt { public static java.lang.Object retire(); }',
  },
];
for (const fixture of terminationOwnerNegativeFixtures) {
  const messages = terminationOwnerFailures([{
    relative: `<negative:${fixture.label}>`,
    blocks: [{ tag: fixture.tag, source: fixture.source }],
  }], 'self-test');
  if (messages.length !== 1) {
    fail(`termination owner negative self-test did not reject ${fixture.label}`);
  } else {
    javaKotlinSemanticNegativeTestCount += 1;
  }
}
for (const fixture of [
  'public final class ZLinkFrameworkRuntime { public void shutdown(); }',
  'public interface ZLinkDrainControl { void drain(); }',
]) {
  const messages = terminationOwnerFailures([{
    relative: '<allowed:host termination>',
    blocks: [{ tag: 'java', source: fixture }],
  }], 'self-test');
  if (messages.length !== 0) {
    fail(`termination owner allowed self-test rejected a host owner: ${messages.join(', ')}`);
  }
}

const validSnapshotFixture = `
public record ZLinkFrameworkRuntimeSnapshot(
    Optional<ZLinkTerminationResult> terminalResult,
    long sequence) {}`;
const snapshotNegativeFixtures = [
  validSnapshotFixture.replace(
    '    Optional<ZLinkTerminationResult> terminalResult,\n', ''),
  validSnapshotFixture.replace(
    '    Optional<ZLinkTerminationResult> terminalResult,',
    '    Optional<ZLinkTerminationResult> terminalResult,\n'
      + '    Optional<ZLinkTerminationResult> terminalResult,'),
  validSnapshotFixture.replace('{}',
    '{ public Optional<ZLinkTerminationResult> terminalResult() { return terminalResult; } }'),
];
if (frameworkSnapshotFailures(validSnapshotFixture).length !== 0) {
  fail('Java host snapshot positive self-test rejected the required record component');
}
for (const [index, fixture] of snapshotNegativeFixtures.entries()) {
  if (frameworkSnapshotFailures(fixture).length === 0) {
    fail(`Java host snapshot negative self-test did not reject mutation ${index + 1}`);
  } else {
    javaKotlinSemanticNegativeTestCount += 1;
  }
}

for (const [relative, fragments] of Object.entries(
  inventory.required_repository_file_fragments || {})) {
  if (!Array.isArray(fragments) || fragments.length === 0) {
    fail(`required_repository_file_fragments must contain non-empty arrays: ${relative}`);
    continue;
  }
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    fail(`required repository document is missing: ${relative}`);
    continue;
  }
  const source = fs.readFileSync(absolute, 'utf8');
  for (const fragment of fragments) {
    if (!source.includes(fragment)) {
      fail(`repository document is missing ${fragment}: ${relative}`);
    }
  }
}
for (const [relative, fragments] of Object.entries(
  inventory.forbidden_repository_file_fragments || {})) {
  if (!Array.isArray(fragments) || fragments.length === 0) {
    fail(`forbidden_repository_file_fragments must contain non-empty arrays: ${relative}`);
    continue;
  }
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    fail(`required repository document is missing: ${relative}`);
    continue;
  }
  const source = fs.readFileSync(absolute, 'utf8');
  for (const fragment of fragments) {
    if (source.includes(fragment)) {
      fail(`repository document contains forbidden fragment ${fragment}: ${relative}`);
    }
  }
}

const formalPaths = [];
for (const fixture of inventory.formal_documents || []) {
  if (!fixture || typeof fixture.path !== 'string'
      || !Array.isArray(fixture.required_fragments)
      || !Array.isArray(fixture.forbidden_fragments)) {
    fail('formal document fixture has an invalid schema');
    continue;
  }
  const absolute = path.join(root, fixture.path);
  if (!fs.existsSync(absolute)) {
    fail(`missing formal document: ${fixture.path}`);
    continue;
  }
  formalPaths.push(fixture.path);
  const source = fs.readFileSync(absolute, 'utf8');
  for (const fragment of fixture.required_fragments) {
    if (!source.includes(fragment)) fail(`formal document is missing ${fragment}: ${fixture.path}`);
  }
  for (const fragment of fixture.forbidden_fragments) {
    if (source.includes(fragment)) fail(`formal document contains removed contract ${fragment}: ${fixture.path}`);
  }
}
// Every formal Korean spec participates in structural validation. The
// inventory above selects semantic owners; it intentionally does not freeze
// prose or code-block hashes.
const allFormalPaths = markdownDocumentsUnder('framework/doc/framework/spec');
if (allFormalPaths.length === 0) fail('formal framework spec document set is empty');
for (const message of codeFenceFailures(root, allFormalPaths)) fail(`formal code fence: ${message}`);
for (const message of relativeMarkdownLinkFailures(root, allFormalPaths)) fail(`formal link: ${message}`);

let targetDocumentCount = 0;
for (const set of inventory.target_document_sets || []) {
  if (!set || typeof set.name !== 'string' || typeof set.directory !== 'string'
      || !Array.isArray(set.required_documents)
      || !Array.isArray(set.readme_required_fragments)
      || !Array.isArray(set.aggregate_required_fragments)
      || !Array.isArray(set.forbidden_code_patterns)
      || !set.required_file_fragments
      || typeof set.required_file_fragments !== 'object') {
    fail('target document set has an invalid schema');
    continue;
  }
  const documents = markdownDocumentsUnder(set.directory);
  targetDocumentCount += documents.length;
  const actualNames = documents.map(relative => path.posix.basename(relative));
  for (const required of set.required_documents) {
    if (!actualNames.includes(required)) fail(`${set.name} is missing required document: ${required}`);
  }
  const readmeRelative = path.posix.join(set.directory, 'README.ko.md');
  const readmeAbsolute = path.join(root, readmeRelative);
  if (!fs.existsSync(readmeAbsolute)) {
    fail(`${set.name} README is missing`);
    continue;
  }
  const readme = fs.readFileSync(readmeAbsolute, 'utf8');
  const sources = documents.map(relative => fs.readFileSync(path.join(root, relative), 'utf8'));
  const aggregate = sources.join('\n');
  for (const fragment of set.readme_required_fragments) {
    if (!readme.includes(fragment)) fail(`${set.name} README is missing no-loss semantic: ${fragment}`);
  }
  for (const fragment of set.aggregate_required_fragments) {
    if (!aggregate.includes(fragment)) fail(`${set.name} is missing target semantic: ${fragment}`);
  }
  for (const [name, fragments] of Object.entries(set.required_file_fragments)) {
    if (!Array.isArray(fragments)) {
      fail(`${set.name} required_file_fragments must contain arrays: ${name}`);
      continue;
    }
    const relative = path.posix.join(set.directory, name);
    const absolute = path.join(root, relative);
    if (!fs.existsSync(absolute)) {
      fail(`${set.name} semantic owner is missing: ${name}`);
      continue;
    }
    const source = fs.readFileSync(absolute, 'utf8');
    for (const fragment of fragments) {
      if (!source.includes(fragment)) fail(`${set.name}/${name} is missing semantic: ${fragment}`);
    }
  }
  const code = sources.flatMap(source => fencedBlocks(source).map(block => block.source)).join('\n');
  for (const expression of set.forbidden_code_patterns) {
    let pattern;
    try {
      pattern = new RegExp(expression, 'u');
    } catch (error) {
      fail(`invalid ${set.name} forbidden-code pattern ${expression}: ${error.message}`);
      continue;
    }
    if (pattern.test(code)) fail(`${set.name} contains removed Core service C ABI code: ${expression}`);
  }
  for (const message of codeFenceFailures(root, documents)) fail(`${set.name} code fence: ${message}`);
  for (const message of relativeMarkdownLinkFailures(root, documents)) fail(`${set.name} link: ${message}`);
}

const consolidation = inventory.plan_consolidation || {};
let forbiddenLegacyPlanCount = 0;
let planDocumentCount = 0;
let consolidatedSemanticCount = 0;
let ledgerRowCount = 0;
let ledgerEdgeCount = 0;
let ledgerProfileCount = 0;
let ledgerProfileRowCount = 0;
let parallelReviewRowCount = 0;
let ledgerExecutionCardCount = 0;
let ledgerFinalUnreachableCount = 0;
if (typeof consolidation.directory !== 'string'
    || !Array.isArray(consolidation.allowed_documents)
    || !Array.isArray(consolidation.ledger_required_fragments)
    || !Array.isArray(consolidation.allowed_profiles)
    || !Array.isArray(consolidation.required_parallel_review_rows)
    || !Array.isArray(consolidation.forbidden_fragments)
    || !Array.isArray(consolidation.required_semantic_owners)
    || !Array.isArray(consolidation.forbidden_legacy_documents)) {
  fail('v11 plan_consolidation has an invalid schema');
} else {
  const allowedDocuments = consolidation.allowed_documents;
  const allowedSet = new Set(allowedDocuments);
  if (allowedDocuments.length !== 24 || allowedSet.size !== allowedDocuments.length
      || allowedDocuments.some(relative => typeof relative !== 'string'
        || relative.length === 0 || path.posix.isAbsolute(relative)
        || relative.split('/').includes('..'))) {
    fail('v11 consolidated plan must declare 24 unique safe relative document paths');
  }
  const actualDocuments = filesUnder(consolidation.directory)
    .map(relative => path.posix.relative(consolidation.directory, relative));
  planDocumentCount = actualDocuments.length;
  const actualSet = new Set(actualDocuments);
  for (const relativeName of allowedDocuments) {
    if (!actualSet.has(relativeName)) {
      fail(`v11 consolidated plan document is missing: ${relativeName}`);
    }
  }
  for (const relativeName of actualDocuments) {
    if (!allowedSet.has(relativeName)) {
      fail(`v11 consolidated plan contains an unowned document: ${relativeName}`);
    }
  }
  const allowedProfiles = new Set(consolidation.allowed_profiles);
  if (consolidation.allowed_profiles.length !== 3
      || allowedProfiles.size !== consolidation.allowed_profiles.length
      || consolidation.allowed_profiles.some(profile => !/^P-[A-Z]+$/u.test(profile))) {
    fail('v11 consolidation must declare 3 unique central profile IDs');
  }
  const parallelReviewRows = new Set(consolidation.required_parallel_review_rows);
  if (consolidation.required_parallel_review_rows.length !== 9
      || parallelReviewRows.size !== consolidation.required_parallel_review_rows.length
      || consolidation.required_parallel_review_rows.some(id => !/^V11-R[A-Z0-9-]*$/u.test(id))) {
    fail('v11 consolidation must declare 9 unique parallel review row IDs');
  }
  if (consolidation.forbidden_fragments.length === 0
      || consolidation.forbidden_fragments.some(fragment => typeof fragment !== 'string'
        || fragment.length === 0)) {
    fail('v11 consolidation forbidden_fragments must contain non-empty strings');
  }

  const semanticIds = new Set();
  if (consolidation.required_semantic_owners.length === 0) {
    fail('v11 consolidation must declare semantic owners');
  }
  for (const owner of consolidation.required_semantic_owners) {
    if (!owner || typeof owner.id !== 'string' || semanticIds.has(owner.id)
        || typeof owner.path !== 'string' || !allowedSet.has(owner.path)
        || !Array.isArray(owner.required_fragments)
        || owner.required_fragments.length === 0
        || owner.required_fragments.some(fragment => typeof fragment !== 'string'
          || fragment.length === 0)) {
      fail('v11 consolidation semantic owner has an invalid schema');
      continue;
    }
    semanticIds.add(owner.id);
    const absolute = path.join(root, consolidation.directory, owner.path);
    if (!fs.existsSync(absolute)) {
      fail(`v11 consolidation semantic owner is missing: ${owner.id}: ${owner.path}`);
      continue;
    }
    consolidatedSemanticCount += 1;
    const source = fs.readFileSync(absolute, 'utf8');
    for (const fragment of owner.required_fragments) {
      if (!source.includes(fragment)) {
        fail(`v11 consolidation semantic is missing: ${owner.id}: ${fragment}`);
      }
    }
  }

  const ledgerRelative = path.posix.join(
    consolidation.directory,
    'route-mesh-11.0.0-execution-ledger.ko.md');
  const ledgerAbsolute = path.join(root, ledgerRelative);
  if (fs.existsSync(ledgerAbsolute)) {
    const ledger = fs.readFileSync(ledgerAbsolute, 'utf8');
    for (const fragment of consolidation.ledger_required_fragments) {
      if (!ledger.includes(fragment)) fail(`v11 execution ledger is missing consolidation semantic: ${fragment}`);
    }
    for (const fragment of consolidation.forbidden_fragments) {
      if (ledger.includes(fragment)) {
        fail(`v11 execution ledger contains a forbidden model policy fragment: ${fragment}`);
      }
    }

    const ledgerLines = ledger.split('\n');
    const executionCards = new Map();
    let inExecutionCardCatalog = false;
    for (const [index, line] of ledgerLines.entries()) {
      if (line === '### 3.4 ID별 execution card catalog') {
        inExecutionCardCatalog = true;
        continue;
      }
      if (inExecutionCardCatalog && /^##\s+/u.test(line)) {
        inExecutionCardCatalog = false;
      }
      if (!inExecutionCardCatalog || !line.startsWith('|')) continue;
      const cells = line.split('|').slice(1, -1).map(cell => cell.trim());
      if (cells.length !== 5) continue;
      const tokens = [...cells[0].matchAll(/`([^`]+)`/gu)].map(match => match[1]);
      if (tokens.length === 0) continue;
      const remainder = cells[0]
        .replace(/`[^`]+`/gu, '')
        .replace(/[\s,]/gu, '');
      if (remainder.length !== 0) {
        fail(`v11 execution card must contain exact IDs only: line ${index + 1}: ${cells[0]}`);
      }
      for (const id of tokens) {
        if (!/^(?:SPEC-[0-9]{2}|V11-[A-Z0-9-]+)$/u.test(id)) {
          fail(`v11 execution card ID is not exact: line ${index + 1}: ${id}`);
          continue;
        }
        if (executionCards.has(id)) {
          fail(`v11 execution ledger contains duplicate execution card ID: ${id}`);
          continue;
        }
        executionCards.set(id, index + 1);
      }
    }
    ledgerExecutionCardCount = executionCards.size;

    const rows = new Map();
    for (const [index, line] of ledgerLines.entries()) {
      if (!line.startsWith('|')) continue;
      const cells = line.split('|').slice(1, -1).map(cell => cell.trim());
      if (cells.length !== 7) continue;
      const match = cells[0].match(/^`((?:SPEC-[0-9]{2}|V11-[A-Z0-9-]+))`$/u);
      if (!match) continue;
      const id = match[1];
      if (rows.has(id)) {
        fail(`v11 execution ledger contains duplicate row ID: ${id}`);
        continue;
      }
      rows.set(id, {
        id,
        line: index + 1,
        assignmentCell: cells[2],
        predecessorCell: cells[3],
        status: cells[4],
        predecessors: [],
      });
    }
    ledgerRowCount = rows.size;
    if (ledgerRowCount === 0) fail('v11 execution ledger contains no executable rows');
    for (const row of rows.values()) {
      if (!executionCards.has(row.id)) {
        fail(`v11 execution ledger row has no execution card: ${row.id}`);
      }
    }
    for (const [id, line] of executionCards) {
      if (!rows.has(id)) {
        fail(`v11 execution card points to an undefined row: ${id}: line ${line}`);
      }
    }

    const usedProfiles = new Set();
    for (const row of rows.values()) {
      const profiles = [...row.assignmentCell.matchAll(/`(P-[A-Z]+)`/gu)]
        .map(match => match[1]);
      if (profiles.length !== 1 || !allowedProfiles.has(profiles[0])) {
        fail(`v11 execution ledger row must select one central profile: ${row.id}: ${row.assignmentCell}`);
        continue;
      }
      if (/gpt-/u.test(row.assignmentCell)) {
        fail(`v11 execution ledger row repeats a raw Codex model name: ${row.id}`);
      }
      usedProfiles.add(profiles[0]);
      ledgerProfileRowCount += 1;
    }
    ledgerProfileCount = usedProfiles.size;
    for (const profile of allowedProfiles) {
      if (!usedProfiles.has(profile)) fail(`v11 execution ledger central profile is unused: ${profile}`);
    }
    for (const id of parallelReviewRows) {
      const row = rows.get(id);
      if (!row) {
        fail(`v11 execution ledger parallel review row is missing: ${id}`);
        continue;
      }
      if (!row.assignmentCell.includes('`P-DEEP`')
          || !row.assignmentCell.includes('Claude `claude-sonnet-5` 병렬 reviewer')) {
        fail(`v11 execution ledger parallel review assignment differs: ${id}`);
        continue;
      }
      parallelReviewRowCount += 1;
    }
    const actualReviewRows = new Set(
      [...rows.keys()].filter(id => /^V11-R[0-9]/u.test(id)));
    for (const id of actualReviewRows) {
      if (!parallelReviewRows.has(id)) {
        fail(`v11 execution ledger review row is not declared for parallel review: ${id}`);
      }
    }
    for (const id of parallelReviewRows) {
      if (!actualReviewRows.has(id)) {
        fail(`v11 parallel review declaration does not identify a review row: ${id}`);
      }
    }
    for (const row of rows.values()) {
      if (!parallelReviewRows.has(row.id)
          && row.assignmentCell.includes('claude-sonnet-5')) {
        fail(`v11 execution ledger assigns the parallel reviewer outside a review row: ${row.id}`);
      }
    }

    for (const row of rows.values()) {
      if (row.predecessorCell === '없음') continue;
      const tokens = [...row.predecessorCell.matchAll(/`([^`]+)`/gu)]
        .map(match => match[1]);
      const remainder = row.predecessorCell
        .replace(/`[^`]+`/gu, '')
        .replace(/[\s,]/gu, '');
      if (tokens.length === 0 || remainder.length !== 0) {
        fail(`v11 execution ledger predecessor must contain exact IDs only: ${row.id}: ${row.predecessorCell}`);
      }
      const unique = new Set();
      for (const predecessor of tokens) {
        if (!/^(?:SPEC-[0-9]{2}|V11-[A-Z0-9-]+)$/u.test(predecessor)) {
          fail(`v11 execution ledger predecessor is not an exact ID: ${row.id}: ${predecessor}`);
          continue;
        }
        if (unique.has(predecessor)) {
          fail(`v11 execution ledger repeats a predecessor: ${row.id}: ${predecessor}`);
          continue;
        }
        unique.add(predecessor);
        row.predecessors.push(predecessor);
        ledgerEdgeCount += 1;
        if (predecessor === row.id) {
          fail(`v11 execution ledger row depends on itself: ${row.id}`);
        } else if (!rows.has(predecessor)) {
          fail(`v11 execution ledger predecessor is undefined: ${row.id}: ${predecessor}`);
        }
      }
    }

    const colors = new Map();
    const stack = [];
    const visit = id => {
      const color = colors.get(id) || 0;
      if (color === 2) return;
      if (color === 1) {
        const start = stack.indexOf(id);
        const cycle = [...stack.slice(start), id];
        fail(`v11 execution ledger dependency cycle: ${cycle.join(' -> ')}`);
        return;
      }
      colors.set(id, 1);
      stack.push(id);
      for (const predecessor of rows.get(id)?.predecessors || []) {
        if (rows.has(predecessor)) visit(predecessor);
      }
      stack.pop();
      colors.set(id, 2);
    };
    for (const id of rows.keys()) visit(id);

    const finalReviewId = 'V11-R7';
    if (!rows.has(finalReviewId)) {
      fail(`v11 execution ledger final review row is missing: ${finalReviewId}`);
    } else {
      const reachesFinal = new Set();
      const collectPredecessors = id => {
        if (reachesFinal.has(id)) return;
        reachesFinal.add(id);
        for (const predecessor of rows.get(id)?.predecessors || []) {
          if (rows.has(predecessor)) collectPredecessors(predecessor);
        }
      };
      collectPredecessors(finalReviewId);
      const unreachable = [...rows.keys()].filter(id => !reachesFinal.has(id));
      ledgerFinalUnreachableCount = unreachable.length;
      for (const id of unreachable) {
        fail(`v11 execution ledger row does not reach ${finalReviewId}: ${id}`);
      }
    }

    const gatedStatuses = new Set(['검토 준비 완료', '리뷰 중', '완료']);
    for (const row of rows.values()) {
      if (!gatedStatuses.has(row.status)) continue;
      for (const predecessor of row.predecessors) {
        const predecessorRow = rows.get(predecessor);
        if (predecessorRow && predecessorRow.status !== '완료') {
          fail(`v11 execution ledger advances before predecessor completion: ${row.id}: ${predecessor}`);
        }
      }
    }
  }
  for (const relativeName of consolidation.forbidden_legacy_documents) {
    forbiddenLegacyPlanCount += 1;
    const absolute = path.join(root, consolidation.directory, relativeName);
    if (fs.existsSync(absolute)) fail(`legacy v11 plan document must not return: ${relativeName}`);
  }
}

// Redis fixtures are semantic wire examples. Validate their schema, field
// order and length-prefixed keys without turning ordinary prose edits into a
// hash failure.
const redisFixtures = [
  ['actor-location-v2.json', 'actor-location-v2'],
  ['authority-store-v1.json', 'authority-store-v1'],
  ['client-server-server-descriptor-v1.json', 'client-server-server-descriptor-v1'],
  ['fanout-publisher-descriptor-v1.json', 'fanout-publisher-descriptor-v1'],
  ['mesh-node-descriptor-v1.json', 'mesh-node-descriptor-v1'],
];
const fixtureDirectory = path.join(root, 'framework/testdata/location/redis');
for (const obsolete of ['actor-transfer-v1.json', 'instance-spot-location-v1.json']) {
  if (fs.existsSync(path.join(fixtureDirectory, obsolete))) {
    fail(`obsolete phase-specific Redis fixture remains: ${obsolete}`);
  }
}
const descriptorKey = (...values) => values
  .map(value => `${Buffer.byteLength(value, 'utf8')}:${value}`).join('');
const serviceWireSchema = JSON.parse(fs.readFileSync(path.join(
  root, 'framework/runtime/protocol/service-wire-v1.schema.json'), 'utf8'));
const authorityKeyProfile = serviceWireSchema.authorityKeyFormat;
const unreservedAuthorityByte = byte => (byte >= 0x41 && byte <= 0x5a)
  || (byte >= 0x61 && byte <= 0x7a)
  || (byte >= 0x30 && byte <= 0x39)
  || [0x2d, 0x2e, 0x5f, 0x7e].includes(byte);
const encodeAuthorityComponent = bytes => `${bytes.length}:` + [...bytes]
  .map(byte => unreservedAuthorityByte(byte)
    ? String.fromCharCode(byte)
    : `%${byte.toString(16).toUpperCase().padStart(2, '0')}`)
  .join('');
const canonicalAuthorityKey = keyContract => {
  const kind = authorityKeyProfile.kindDiscriminators.find(
    candidate => candidate.objectKind === keyContract?.objectKind);
  const identityHex = keyContract?.identityHex || '';
  if (!kind || typeof keyContract?.meshName !== 'string'
      || !/^(?:[0-9a-fA-F]{2})+$/u.test(identityHex)) return undefined;
  const mesh = Buffer.from(keyContract.meshName, 'utf8');
  const identity = Buffer.from(identityHex, 'hex');
  return [authorityKeyProfile.prefix, kind.wire,
    encodeAuthorityComponent(mesh), encodeAuthorityComponent(identity)]
    .join(authorityKeyProfile.separator);
};
const validateHashFields = (fixtureName, fixture, hash) => {
  if (!Array.isArray(fixture.hashFields)
      || new Set(fixture.hashFields).size !== fixture.hashFields.length
      || !hash || typeof hash !== 'object'
      || JSON.stringify(Object.keys(hash)) !== JSON.stringify(fixture.hashFields)) {
    fail(`Redis fixture hash field schema differs: ${fixtureName}`);
  }
};
const positiveDecimal = value => typeof value === 'string'
  && /^[1-9]\d*$/u.test(value)
  && BigInt(value) <= 9223372036854775807n;
const validateAuthorityHash = (fixtureName, fixture, hash) => {
  validateHashFields(fixtureName, fixture, hash);
  if (typeof hash?.payload !== 'string' || hash.payload.length === 0
      || !positiveDecimal(hash.storeVersion)
      || !positiveDecimal(hash.objectGeneration)
      || !positiveDecimal(hash.authorityOwnerGeneration)
      || typeof hash.ownerId !== 'string' || hash.ownerId.length === 0
      || !positiveDecimal(hash.ownerLeaseGeneration)
      || Object.hasOwn(hash, 'leaseExpiresAtMs')) {
    fail(`Redis authority fixture metadata differs: ${fixtureName}`);
  }
};
const validateHashRow = (fixtureName, row, expectedKey = undefined) => {
  if (!row || typeof row !== 'object' || !row.hash || typeof row.hash !== 'object') {
    fail(`Redis fixture row/hash is missing: ${fixtureName}`);
    return undefined;
  }
  if (expectedKey !== undefined && row.key !== expectedKey) {
    fail(`Redis fixture length-prefixed key differs: ${fixtureName}`);
  }
  let payload;
  try {
    payload = JSON.parse(row.hash.json);
  } catch (error) {
    fail(`Redis fixture embedded JSON is invalid: ${fixtureName}: ${error.message}`);
    return undefined;
  }
  if (row.hash.json !== JSON.stringify(payload)) {
    fail(`Redis fixture embedded JSON is not canonical compact JSON: ${fixtureName}`);
  }
  return payload;
};
const descriptorFieldOrder = {
  'client-server-server-descriptor-v1.json': [
    'ChannelName', 'ServerRid', 'LifecycleGeneration', 'DescriptorRevision',
    'NormalizedEffectiveMaxMessageBytes', 'Endpoint', 'Weight', 'State',
    'SecurityIdentity', 'OwnerId', 'OwnerLeaseGeneration', 'UpdatedAt',
  ],
  'fanout-publisher-descriptor-v1.json': [
    'ChannelName', 'PublisherRid', 'LifecycleGeneration', 'DescriptorRevision',
    'Endpoint', 'State', 'SecurityIdentity', 'OwnerId', 'OwnerLeaseGeneration',
    'UpdatedAt',
  ],
  'mesh-node-descriptor-v1.json': [
    'MeshName', 'Rid', 'LifecycleGeneration', 'DescriptorRevision',
    'NormalizedEffectiveMaxMessageBytes', 'Endpoint', 'ChannelWeights',
    'ApplicationVersion', 'SpotTypes', 'StatefulCapabilities',
    'ProtocolCapabilities', 'MaintenanceWave', 'State', 'SecurityIdentity',
    'OwnerId', 'OwnerLeaseGeneration', 'UpdatedAt',
  ],
};
const runtimeStates = new Set(['Preparing', 'Serving', 'Draining', 'Stopped', 'Error']);
const compareUtf8 = (left, right) => Buffer.compare(
  Buffer.from(left, 'utf8'), Buffer.from(right, 'utf8'));
const isStrictlyUtf8Sorted = values => Array.isArray(values)
  && values.every((value, index) => typeof value === 'string'
    && (index === 0 || compareUtf8(values[index - 1], value) < 0));
const validateDescriptorPayload = (name, payload) => {
  const expectedFields = descriptorFieldOrder[name];
  if (!expectedFields) return;
  if (JSON.stringify(Object.keys(payload)) !== JSON.stringify(expectedFields)) {
    fail(`Redis descriptor canonical field order differs: ${name}`);
  }
  if (!runtimeStates.has(payload.State)) {
    fail(`Redis descriptor FrameworkRuntimeState differs: ${name}`);
  }
  for (const field of ['LifecycleGeneration', 'DescriptorRevision', 'OwnerLeaseGeneration']) {
    if (!Number.isSafeInteger(payload[field]) || payload[field] <= 0) {
      fail(`Redis descriptor ${field} must be a positive safe integer: ${name}`);
    }
  }
  if (typeof payload.OwnerId !== 'string' || payload.OwnerId.length === 0) {
    fail(`Redis descriptor OwnerId is missing: ${name}`);
  }
  if (name !== 'fanout-publisher-descriptor-v1.json'
      && (!Number.isSafeInteger(payload.NormalizedEffectiveMaxMessageBytes)
        || payload.NormalizedEffectiveMaxMessageBytes <= 0
        || payload.NormalizedEffectiveMaxMessageBytes > 0xffffffff)) {
    fail(`Redis descriptor normalized message bound differs: ${name}`);
  }
  if (name !== 'mesh-node-descriptor-v1.json') return;
  const channelNames = Object.keys(payload.ChannelWeights || {});
  if (!isStrictlyUtf8Sorted(channelNames)) {
    fail('MeshNode descriptor ChannelWeights must be UTF-8 sorted and unique');
  }
  for (const field of ['SpotTypes', 'ProtocolCapabilities']) {
    if (!isStrictlyUtf8Sorted(payload[field])) {
      fail(`MeshNode descriptor ${field} must be UTF-8 sorted and unique`);
    }
  }
  if (!payload.ProtocolCapabilities.includes(serviceWireSchema.protocol.requiredCapability)) {
    fail('MeshNode descriptor is missing the required service protocol capability');
  }
  if (!Number.isSafeInteger(payload.ApplicationVersion) || payload.ApplicationVersion < 0) {
    fail('MeshNode descriptor ApplicationVersion must be a non-negative JSON integer');
  }
  if (!(payload.MaintenanceWave === null || typeof payload.MaintenanceWave === 'string')) {
    fail('MeshNode descriptor MaintenanceWave must be a string or null');
  }
  if (!Array.isArray(payload.StatefulCapabilities)
      || payload.StatefulCapabilities.length > 1024) {
    fail('MeshNode descriptor StatefulCapabilities count differs');
    return;
  }
  const kindOrder = { actor: 1, instanceSpot: 2 };
  let previousCapability;
  for (const capability of payload.StatefulCapabilities) {
    if (JSON.stringify(Object.keys(capability)) !== JSON.stringify([
      'ObjectKind', 'Type', 'TransferPolicy', 'ReadableStateContractIds', 'Available'])) {
      fail('MeshNode descriptor StatefulCapabilities field order differs');
      continue;
    }
    if (kindOrder[capability.ObjectKind] === undefined
        || typeof capability.Type !== 'string' || capability.Type.length === 0
        || !['disabled', 'recreate', 'snapshot'].includes(capability.TransferPolicy)
        || !isStrictlyUtf8Sorted(capability.ReadableStateContractIds)
        || !Number.isSafeInteger(capability.Available)
        || capability.Available < 0) {
      fail('MeshNode descriptor StatefulCapabilities value differs');
    }
    if (capability.TransferPolicy === 'snapshot'
        ? capability.ReadableStateContractIds.length === 0
        : capability.ReadableStateContractIds.length !== 0) {
      fail('MeshNode descriptor transfer policy and readable contracts differ');
    }
    const current = `${capability.ObjectKind}\u0000${capability.Type}`;
    if (previousCapability !== undefined) {
      const [previousKind, previousType] = previousCapability.split('\u0000');
      if (kindOrder[previousKind] > kindOrder[capability.ObjectKind]
          || (previousKind === capability.ObjectKind
            && compareUtf8(previousType, capability.Type) >= 0)) {
        fail('MeshNode descriptor StatefulCapabilities must be sorted and unique');
      }
    }
    previousCapability = current;
  }
};
let redisFixtureCount = 0;
for (const [name, format] of redisFixtures) {
  const absolute = path.join(fixtureDirectory, name);
  if (!fs.existsSync(absolute)) {
    fail(`missing Redis semantic fixture: ${name}`);
    continue;
  }
  let fixture;
  try {
    fixture = JSON.parse(fs.readFileSync(absolute, 'utf8'));
  } catch (error) {
    fail(`invalid Redis semantic fixture JSON: ${name}: ${error.message}`);
    continue;
  }
  redisFixtureCount += 1;
  if (fixture.format !== format) fail(`Redis fixture format differs: ${name}`);
  if (name === 'authority-store-v1.json') {
    const expectedKey = canonicalAuthorityKey(fixture.keyContract);
    if (!expectedKey || fixture.keyContract?.authorityKey !== expectedKey) {
      fail('Authority Store fixture key differs from authority-key-v1');
    }
    for (const transition of ['newObject', 'preserve', 'newOwner']) {
      validateAuthorityHash(name, fixture, fixture[transition]?.hash);
    }
    const newObjectVersion = BigInt(fixture.newObject?.hash?.storeVersion ?? '-1');
    const preserveVersion = BigInt(fixture.preserve?.hash?.storeVersion ?? '-1');
    const newOwnerVersion = BigInt(fixture.newOwner?.hash?.storeVersion ?? '-1');
    const deleteVersion = BigInt(fixture.delete?.consumedStoreRevision ?? '-1');
    if (fixture.missing?.kind !== 'Missing'
        || Object.hasOwn(fixture.missing || {}, 'storeVersion')
        || fixture.newObject?.expectation !== 'Missing'
        || fixture.newObject?.result !== 'Stored'
        || fixture.preserve?.expectation?.kind !== 'Found'
        || fixture.preserve?.expectation?.storeVersion
          !== fixture.newObject?.hash?.storeVersion
        || fixture.newOwner?.expectation?.kind !== 'Found'
        || fixture.newOwner?.expectation?.storeVersion !== fixture.preserve?.hash?.storeVersion
        || fixture.delete?.expectation?.kind !== 'Found'
        || fixture.delete?.expectation?.storeVersion !== fixture.newOwner?.hash?.storeVersion
        || fixture.delete?.result !== 'Deleted'
        || Object.hasOwn(fixture.missingAfterDelete || {}, 'storeVersion')
        || !(newObjectVersion < preserveVersion
          && preserveVersion < newOwnerVersion && newOwnerVersion < deleteVersion)) {
      fail('Authority Store fixture CAS version transition differs');
    }
    const created = fixture.newObject?.hash || {};
    const preserved = fixture.preserve?.hash || {};
    const replaced = fixture.newOwner?.hash || {};
    if (created.objectGeneration !== preserved.objectGeneration
        || created.objectGeneration !== replaced.objectGeneration
        || created.authorityOwnerGeneration !== preserved.authorityOwnerGeneration
        || BigInt(replaced.authorityOwnerGeneration || '0')
          <= BigInt(preserved.authorityOwnerGeneration || '0')
        || fixture.generationExhausted?.counterAtMaximum
          !== serviceWireSchema.authorityStoreGenerationProfile.generationMaximum
        || fixture.generationExhausted?.result !== 'GenerationExhausted'
        || fixture.generationExhausted?.retriable !== false
        || fixture.generationExhausted?.rowIndexAndCounterMutationCount !== 0) {
      fail('Authority Store fixture generation transition differs');
    }
    continue;
  }
  if (name === 'actor-location-v2.json') {
    const expectedKey = canonicalAuthorityKey(fixture.keyContract);
    if (!expectedKey || fixture.keyContract?.authorityKey !== expectedKey
        || fixture.row?.key !== expectedKey) {
      fail('Actor authority fixture key differs from authority-key-v1');
    }
    validateAuthorityHash(name, fixture, fixture.row?.hash);
    continue;
  }
  const payload = validateHashRow(name, fixture.row);
  if (!payload) continue;
  validateDescriptorPayload(name, payload);
  const identityFields = {
    'actor-location-v2.json': ['MeshName', 'ActorId'],
    'client-server-server-descriptor-v1.json': ['ChannelName', 'ServerRid'],
    'fanout-publisher-descriptor-v1.json': ['ChannelName', 'PublisherRid'],
    'mesh-node-descriptor-v1.json': ['MeshName', 'Rid'],
  };
  const fields = identityFields[name];
  if (!fields || fields.some(field => typeof payload[field] !== 'string')) {
    fail(`Redis fixture embedded identity is missing: ${name}`);
  } else if (fixture.row.key !== descriptorKey(...fields.map(field => payload[field]))) {
    fail(`Redis fixture key differs from embedded identity: ${name}`);
  }
  validateHashFields(name, fixture, fixture.row.hash);
  if (payload.OwnerId !== fixture.row.hash.owner
      || String(payload.OwnerLeaseGeneration) !== fixture.row.hash.gen) {
    fail(`Redis descriptor fixture owner lease token differs: ${name}`);
  }
}

const exactSemanticFixtures = [
  ['cpp', ['07-location-maintenance.ko.md', '07-location-store.ko.md'],
    ['authority_generation_exhausted_t', 'owner_lease_generation_exhausted_t',
      'asynchronous operation이 끝날 때까지', 'opaque equality token', 'Store가 없는 Actor']],
  ['dotnet', ['08-authority-checkpoint.ko.md', '08-location-maintenance.ko.md'],
    ['record GenerationExhausted', 'underlying storage는 asynchronous operation이 끝날 때까지',
      'Global lease generation counter가 `long.MaxValue`', '`LifecycleGeneration`은 0이 아닌 opaque equality token',
      'Store 없이 만든 Actor']],
  ['java', ['location-maintenance.ko.md'],
    ['ZLinkAuthorityGenerationExhausted', 'ZLinkOwnerLeaseGenerationExhausted',
      '`byte[]`은 반환된 `CompletionStage`가 완료될 때까지', '`lifecycleGeneration`은 0이 아닌 opaque equality token',
      'Store 없이 만든 Actor']],
  ['kotlin', ['location-maintenance.ko.md'],
    ['ZLinkAuthorityGenerationExhausted', 'ZLinkOwnerLeaseGenerationExhausted',
      '`byte[]`은 `CompletionStage`가 완료될 때까지', '`lifecycleGeneration`은 0이 아닌 opaque equality token',
      'Store 없이 만든 Actor']],
  ['node', ['08-location-maintenance.ko.md'],
    ['kind: "generationExhausted"', '`Uint8Array`는 반환된 Promise가 settle될 때까지',
      '`Date` 객체', '`lifecycleGeneration`은 0이 아닌 opaque equality token', 'Store 없이 만든 Actor']],
];
for (const [language, files, required] of exactSemanticFixtures) {
  const directory = path.posix.join(
    'framework/doc/framework/spec/server/languages', language, 'interfaces');
  const source = files.map(name => fs.readFileSync(path.join(root, directory, name), 'utf8')).join('\n');
  for (const fragment of required) {
    if (!source.includes(fragment)) {
      fail(`${language} generation/lifetime exact contract is missing: ${fragment}`);
    }
  }
  if (/(?:OwnerLeaseFencingMargin|ownerLeaseFencingMargin|owner_lease_fencing_margin)[^\n]{0,80}(?:1초|FromSeconds\(1\)|1000)/u.test(source)) {
    fail(`${language} exact contract retains a one-second owner lease fencing margin`);
  }
  const allExactSource = markdownDocumentsUnder(directory)
    .map(relative => fs.readFileSync(path.join(root, relative), 'utf8')).join('\n');
  if (/(?:location generation|activation epoch)/iu.test(allExactSource)) {
    fail(`${language} exact contract retains a removed location generation or activation epoch name`);
  }
}

const lifecycleSemanticFixtures = [
  ['cpp', '05-actors.ko.md', '04-spots.ko.md', '02-configuration-host.ko.md'],
  ['dotnet', '06-actors.ko.md', '05-spots.ko.md', '10-topology-monitoring.ko.md'],
  ['java', 'actors.ko.md', 'spots.ko.md', 'common-runtime.ko.md'],
  ['kotlin', 'actors.ko.md', 'spots.ko.md', 'common-runtime.ko.md'],
  ['node', '05-actors.ko.md', '04-spots.ko.md', '03-location-observability.ko.md'],
];
for (const [language, actorFile, spotFile, hostFile] of lifecycleSemanticFixtures) {
  const directory = path.posix.join(
    'framework/doc/framework/spec/server/languages', language, 'interfaces');
  const actor = fs.readFileSync(path.join(root, directory, actorFile), 'utf8');
  const spot = fs.readFileSync(path.join(root, directory, spotFile), 'utf8');
  const host = fs.readFileSync(path.join(root, directory, hostFile), 'utf8');
  if (!/connection-bound one-way/iu.test(actor)) {
    fail(`${language} Actor lifecycle contract is missing connection-bound one-way`);
  }
  if (!actor.includes('Entry Spot member')) {
    fail(`${language} Actor lifecycle contract is missing Entry Spot membership`);
  }
  if (!/(?:hidden|숨은)\s+retry/iu.test(actor)) {
    fail(`${language} Actor lifecycle contract is missing no-hidden-retry semantics`);
  }
  for (const fragment of ['active Actor membership', 'Cold Instance factory']) {
    if (!spot.includes(fragment)) fail(`${language} Spot lifecycle contract is missing: ${fragment}`);
  }
  if (!/Blocked\/DeadlineExceeded|blocked\/deadline_exceeded/u.test(host)
      || !/ForceStopped\/DeadlineExceeded|force_stopped\/deadline_exceeded/u.test(host)) {
    fail(`${language} Retire deadline phase contract is incomplete`);
  }
}

if (failures.length) {
  process.stderr.write(`${failures.map(message => `FAIL: ${message}`).join('\n')}\n`);
  process.exit(1);
}
process.stdout.write(
  `FRAMEWORK DOC CONTRACTS CLEAN version=${inventory.version} languages=${expectedLanguages.length}`
  + ` exact_documents=${exactDocumentCount} exact_fences=${exactFenceCount}`
  + ` package_contract_fences=${exactFenceRoleCounts.get('package-contract')}`
  + ` application_example_fences=${exactFenceRoleCounts.get('application-example')}`
  + ` documentation_support_fences=${exactFenceRoleCounts.get('documentation-support')}`
  + ` syntax_and_example_fences=${syntaxAndExampleFenceCount}`
  + ` package_declaration_owners=${packageDeclarationOwnerCount}`
  + ` block_role_negative_mutations=${blockRoleNegativeMutationCount}`
  + ` formal_documents=${allFormalPaths.length}`
  + ` semantic_owners=${formalPaths.length} target_documents=${targetDocumentCount}`
  + ` plan_documents=${planDocumentCount} consolidated_semantics=${consolidatedSemanticCount}`
  + ` ledger_rows=${ledgerRowCount} ledger_edges=${ledgerEdgeCount}`
  + ` ledger_profiles=${ledgerProfileCount} ledger_profile_rows=${ledgerProfileRowCount}`
  + ` parallel_review_rows=${parallelReviewRowCount}`
  + ` ledger_execution_cards=${ledgerExecutionCardCount}`
  + ` ledger_final_unreachable=${ledgerFinalUnreachableCount}`
  + ` java_kotlin_negative_mutations=${javaKotlinSemanticNegativeTestCount}`
  + ` termination_negative_mutations=${terminationContractNegativeTestCount}`
  + ` redis_fixtures=${redisFixtureCount} legacy_plan_absent=${forbiddenLegacyPlanCount}\n`);
NODE
