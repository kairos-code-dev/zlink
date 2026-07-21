#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

node - "$repo_root" <<'NODE'
'use strict';

const fs = require('fs');
const path = require('path');
const root = process.argv[2];
const { readExactContract } = require(
  path.join(root, 'scripts/lib/framework-contract-documents.cjs'));
const languages = ['dotnet', 'cpp', 'java', 'kotlin', 'node'];
const tags = {
  dotnet: ['csharp'], cpp: ['cpp'], java: ['java'],
  kotlin: ['kotlin', 'java'], node: ['ts', 'typescript'],
};
const failures = [];
const fail = message => failures.push(message);

function read(relative) {
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    fail(`missing Instance Spot formal contract: ${relative}`);
    return '';
  }
  return fs.readFileSync(absolute, 'utf8');
}

const formalFixtures = [
  {
    path: 'framework/doc/framework/spec/05-framework-api.ko.md',
    required: [
      'actor-free Instance Spot factory',
      'InstanceSpotAddress는 `MeshName`, `InstanceSpotType`, `SpotRid`만 가진다',
      '`MaxActiveInstances=4096`',
      '`ActivationTimeout=3초`',
      'Caller는 `createIfMissing`, target node, owner token, generation이나 retry option을 전달하지 않는다'
    ]
  },
  {
    path: 'framework/doc/framework/spec/server/20-spot-messaging.ko.md',
    required: [
      'Instance cold send도 source outbound admission에서 같은 결과를 완료하며',
      'target activation queue 수락을\n기다리지 않는다',
      'Target runtime은 location owner claim을 수행하지 않는다'
    ]
  },
  {
    path: 'framework/doc/framework/spec/server/24-spot-address-messaging.ko.md',
    required: [
      '동기\n`TrySubmit`이나 cache 상태에 따라 의미가 달라지는 호출은 제공하지 않는다',
      'Location을 `Ready`로 commit하고 barrier를 연 뒤 첫 message를 일반 Spot application queue에 한 번 제출한다',
      'target queue admission 뒤 다른 owner에게 숨은 retry를 수행하지 않는다',
      'Instance Spot은 application이 target MeshNode를 선택하는 별도 `Create`\u00b7`GetOrCreate` operation을 제공하지'
    ]
  },
  {
    path: 'framework/doc/framework/spec/server/40-location-runtime.ko.md',
    required: [
      'Source coordinator가 target 선택과 authority claim을 소유하고 target runtime은 exact fence 검증',
      'Target은 exact authority, target node lifecycle과 host lease를 다시 확인한다. Target이 authority를 claim하지',
      'Serving 전 initial authority scan과',
      'MeshNode descriptor의 Instance capability는\n등록한 type, transfer policy',
      'ObjectGeneration, AuthorityOwnerGeneration과 새 StoreVersion',
      'current OwnerId와 OwnerLeaseGeneration',
      'Public callback에 TransferId를\n노출하지 않는다'
    ]
  },
  {
    path: 'framework/doc/framework/spec/server/51-runtime-metrics.ko.md',
    required: [
      'zlink.instance_spot.activations',
      'zlink.instance_spot.activation.duration',
      'zlink.instance_spot.pending.messages',
      'zlink.instance_spot.pending.bytes',
      'zlink.instance_spot.claim.conflicts',
      'zlink.instance_spot.takeovers'
    ]
  }
];
for (const fixture of formalFixtures) {
  const source = read(fixture.path);
  for (const fragment of fixture.required) {
    if (!source.includes(fragment)) fail(`formal contract is missing ${fragment}: ${fixture.path}`);
  }
}

const contracts = new Map(languages.map(language => [
  language, readExactContract(root, language, tags[language]),
]));
const projections = {
  dotnet: [
    'public sealed record InstanceSpotAddress(',
    'public sealed record ZLinkInstanceSpotFactoryOptions',
    'public interface IZLinkInstanceSpot',
    'AddInstanceSpotFactory<TSpot>',
    'public interface IZLinkAuthorityStore',
    'public interface IZLinkLocationStore :',
    'source local outbound admission'
  ],
  cpp: [
    'struct instance_spot_address_t',
    'struct instance_spot_factory_options_t',
    'class instance_spot_t {',
    'add_instance_spot_factory(',
    'class authority_store_t',
    'class location_store_t : public mesh_node_location_store_t,',
    'local outbound admission'
  ],
  java: [
    'public record InstanceSpotAddress(',
    'public record ZLinkInstanceSpotFactoryOptions(',
    'public interface ZLinkInstanceSpot',
    'addInstanceSpotFactory(',
    'public interface ZLinkAuthorityStore',
    'public interface ZLinkLocationStore extends'
  ],
  kotlin: [
    'Instance Spot은 Java builder를 직접 사용하므로',
    'target: InstanceSpotAddress',
    'Kotlin은 Java `ZLinkAuthorityStore`',
    'Actor·Instance phase별 Store나 application이 transfer phase를 조립하는 extension을 추가하지 않는다'
  ],
  node: [
    'export interface InstanceSpotAddress',
    'export interface ZLinkInstanceSpotFactoryOptions',
    'export interface ZLinkInstanceSpot',
    'addInstanceSpotFactory<TSpot extends ZLinkInstanceSpot>',
    'export interface ZLinkAuthorityStore',
    'export interface ZLinkLocationStore extends',
    'source local outbound admission까지 기다리지만'
  ]
};
for (const [language, fragments] of Object.entries(projections)) {
  const source = contracts.get(language).source;
  const normalizedSource = source.replace(/\s+/gu, ' ');
  for (const fragment of fragments) {
    const normalizedFragment = fragment.replace(/\s+/gu, ' ');
    if (!normalizedSource.includes(normalizedFragment)) {
      fail(`${language} Instance Spot projection is missing: ${normalizedFragment}`);
    }
  }
  const code = contracts.get(language).code;
  if (/\b(?:TrySubmit|trySubmit|try_submit)\s*(?:<[^>]*>)?\s*\(/.test(code)) {
    fail(`${language} Instance Spot public declarations expose TrySubmit`);
  }
  if (/\bzlink_(?:instance_spot|spot_(?:send|request)_to_instance)[a-z0-9_]*\s*\(/.test(code)) {
    fail(`${language} exact contract exposes removed Core Instance service ABI`);
  }
  if (/\b(?:IZLinkInstanceSpotLocationStore|instance_spot_location_store_t|ZLinkInstanceSpotLocationStore|ZLinkInstanceSpotStore)\b/.test(code)) {
    fail(`${language} exact contract exposes a phase-specific Instance Store`);
  }
  if (/\b(?:CreateInstanceSpot|GetOrCreateInstanceSpot|createInstanceSpot|getOrCreateInstanceSpot|create_instance_spot|get_or_create_instance_spot)\b/.test(code)) {
    fail(`${language} exact contract exposes a target-selecting Instance lifecycle operation`);
  }
  if (language === 'cpp' && /class\s+instance_spot_t\s*:\s*public\s+spot_t\b/.test(code)) {
    fail('cpp actor-free Instance Spot still inherits the User Spot actor-capable base');
  }
}

function requireAddress(source, pattern, fields, language) {
  const match = pattern.exec(source);
  if (!match) {
    fail(`${language} InstanceSpotAddress declaration is missing`);
    return;
  }
  const body = match[1];
  let previous = -1;
  for (const field of fields) {
    const index = body.indexOf(field);
    if (index < 0 || index <= previous) {
      fail(`${language} InstanceSpotAddress must preserve only MeshName, type and Spot RID order`);
      break;
    }
    previous = index;
  }
  if (/owner|generation|epoch|nodeRid|node_rid|createIfMissing|create_if_missing/i.test(body)) {
    fail(`${language} InstanceSpotAddress leaks placement or authority state`);
  }
}
requireAddress(
  contracts.get('dotnet').source,
  /public sealed record InstanceSpotAddress\(([\s\S]*?)\);/,
  ['string MeshName', 'string InstanceSpotType', 'RoutingId SpotRid'], 'dotnet');
requireAddress(
  contracts.get('cpp').source,
  /struct instance_spot_address_t\s*\{([\s\S]*?)\};/,
  ['std::string mesh_name', 'std::string instance_spot_type', 'spot_rid_t spot_rid'], 'cpp');
requireAddress(
  contracts.get('java').source,
  /public record InstanceSpotAddress\(([\s\S]*?)\)\s*\{\}/,
  ['String meshName', 'String instanceSpotType', 'RoutingId spotRid'], 'java');
requireAddress(
  contracts.get('node').source,
  /export interface InstanceSpotAddress\s*\{([\s\S]*?)\}/,
  ['meshName: string', 'instanceSpotType: string', 'spotRid: RoutingId'], 'node');

const authorityOperations = {
  dotnet: ['ReadAuthorityAsync', 'CompareExchangeAuthorityAsync'],
  cpp: ['read_authority', 'compare_exchange_authority'],
  java: ['read(', 'compareExchange('],
  node: ['readAuthority(', 'compareExchangeAuthority('],
};
for (const [language, operations] of Object.entries(authorityOperations)) {
  const source = contracts.get(language).source;
  for (const operation of operations) {
    if (!source.includes(operation)) fail(`${language} opaque authority store is missing ${operation}`);
  }
}

if (failures.length) {
  process.stderr.write(`${failures.map(message => `FAIL: ${message}`).join('\n')}\n`);
  process.exit(1);
}
process.stdout.write(
  `INSTANCE SPOT DOC CONTRACTS CLEAN languages=${languages.length}`
  + ` formal_documents=${formalFixtures.length} address_fields=12 authority_operations=8\n`);
NODE
