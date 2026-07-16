#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
inventory="$repo_root/framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json"

node - "$repo_root" "$inventory" <<'NODE'
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');

const root = process.argv[2];
const inventoryPath = process.argv[3];
const inventory = JSON.parse(fs.readFileSync(inventoryPath, 'utf8'));
const failures = [];

if (inventory.version !== '10.0.0' || inventory.language !== 'dotnet') {
  failures.push('inventory must describe the .NET 10.0.0 contract');
}

const docs = new Map();
for (const relative of inventory.documents) {
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    failures.push(`missing contract document: ${relative}`);
    continue;
  }
  docs.set(relative, fs.readFileSync(absolute, 'utf8'));
}

const combined = [...docs.values()].join('\n');
const declaredCounts = new Map();
const typePattern = /public\s+(?:(?:sealed|abstract|readonly)\s+)*(?:class|interface|enum|record(?:\s+struct)?)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
const delegatePattern = /public\s+delegate\s+[A-Za-z_][A-Za-z0-9_<>,.?\[\]\s]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g;
for (const pattern of [typePattern, delegatePattern]) {
  let match;
  while ((match = pattern.exec(combined)) !== null) {
    declaredCounts.set(match[1], (declaredCounts.get(match[1]) || 0) + 1);
  }
}

const declared = new Set(declaredCounts.keys());
const expected = new Set(inventory.dotnet_public_symbols);
for (const symbol of [...expected].sort()) {
  if (!declared.has(symbol)) failures.push(`inventory symbol is not declared: ${symbol}`);
}
for (const symbol of [...declared].sort()) {
  if (!expected.has(symbol)) failures.push(`public declaration missing from inventory: ${symbol}`);
}
for (const [symbol, count] of [...declaredCounts].sort(([left], [right]) => left.localeCompare(right))) {
  const expectedCount = inventory.allowed_declaration_counts[symbol] || 1;
  if (count !== expectedCount) {
    failures.push(`public declaration count differs: ${symbol} expected=${expectedCount} actual=${count}`);
  }
}

for (const [relative, fixture] of Object.entries(inventory.csharp_fixture_sha256)) {
  const source = docs.get(relative);
  if (source === undefined) {
    failures.push(`C# fixture document is outside inventory scope: ${relative}`);
    continue;
  }
  const blocks = [...source.matchAll(/```csharp\n([\s\S]*?)```/g)]
    .map(match => match[1].replace(/[ \t]+$/gm, '').trim());
  const normalized = blocks.join('\n---BLOCK---\n');
  const digest = crypto.createHash('sha256').update(normalized).digest('hex');
  if (blocks.length !== fixture.block_count || digest !== fixture.sha256) {
    failures.push(
      `C# exact fixture differs: ${relative} blocks=${blocks.length}/${fixture.block_count} sha256=${digest}/${fixture.sha256}`);
  }
}

for (const symbol of inventory.required_method_symbols) {
  const pattern = new RegExp(`\\b${symbol.replace(/[.*+?^${}()|[\\]\\]/g, '\\$&')}\\s*(?:<[^;{()]*>)?\\s*\\(`);
  if (!pattern.test(combined)) failures.push(`required method is not declared: ${symbol}`);
}

for (const symbol of inventory.forbidden_surface) {
  if (combined.includes(symbol)) failures.push(`forbidden 10.0.0 surface remains: ${symbol}`);
}

for (const [relative, text] of docs) {
  if (text.includes('framework/doc/plan/v10.0/')) {
    failures.push(`formal contract references temporary plan: ${relative}`);
  }
  const fences = (text.match(/^```/gm) || []).length;
  if (fences % 2 !== 0) failures.push(`unbalanced code fence: ${relative}`);

  const anchors = new Set();
  for (const line of text.split(/\r?\n/)) {
    const heading = /^(#{1,6})\s+(.+?)\s*$/.exec(line);
    if (!heading) continue;
    const anchor = heading[2]
      .toLowerCase()
      .replace(/`/g, '')
      .replace(/[^\p{L}\p{N}\s_-]/gu, '')
      .trim()
      .replace(/\s+/g, '-');
    if (anchors.has(anchor)) failures.push(`duplicate heading anchor ${anchor}: ${relative}`);
    anchors.add(anchor);
  }

  const linkPattern = /\[[^\]]*\]\(([^)]+)\)/g;
  let link;
  while ((link = linkPattern.exec(text)) !== null) {
    const target = link[1].trim();
    if (!target || target.startsWith('#') || /^[a-z]+:/i.test(target)) continue;
    const filePart = target.split('#', 1)[0];
    if (!filePart) continue;
    const resolved = path.resolve(path.dirname(path.join(root, relative)), decodeURIComponent(filePart));
    if (!fs.existsSync(resolved)) failures.push(`broken relative link ${target}: ${relative}`);
  }
}

if (failures.length > 0) {
  for (const failure of failures) process.stderr.write(`FAIL: ${failure}\n`);
  process.exit(1);
}

process.stdout.write(
  `FRAMEWORK DOC CONTRACTS CLEAN symbols=${declared.size} methods=${inventory.required_method_symbols.length} documents=${docs.size}\n`);
NODE
