#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:---check}"

node - "$repo_root" "$mode" <<'NODE'
'use strict';

const fs = require('fs');
const path = require('path');

const root = process.argv[2];
const mode = process.argv[3];
if (!['--check', '--self-test'].includes(mode)) {
  process.stderr.write('usage: verify-framework-instance-spot-contracts.sh [--check|--self-test]\n');
  process.exit(2);
}

const {readExactContract} = require(
  path.join(root, 'scripts/lib/framework-contract-documents.cjs'));
const languages = ['dotnet', 'cpp', 'java', 'kotlin', 'node'];
const tags = {
  dotnet: ['csharp'],
  cpp: ['cpp'],
  java: ['java'],
  kotlin: ['kotlin', 'java'],
  node: ['ts', 'typescript'],
};

const read = relative => {
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) throw new Error(`missing contract document: ${relative}`);
  return fs.readFileSync(absolute, 'utf8');
};
const normalized = source => source.replace(/\s+/gu, ' ').trim();

const formalFixtures = [
  {
    path: 'framework/doc/framework/spec/05-framework-api.ko.md',
    required: [
      'actor-free Instance Spot factory',
      'Instance Spot은 actor-free lifecycle을 사용하며 Actor handler, Actor membership과 Logical Multicast subscription을 등록할 수 없다.',
      'Actor manager와 Spot manager는 global ID를 받는 `Create`, `GetOrCreate`, `Find` family를 제공한다.',
      'Public object handle, directory, resolver와 unbounded list는 제공하지 않는다.',
      '| exact SpotRef close |',
    ],
  },
  {
    path: 'framework/doc/framework/spec/server/20-spot-messaging.ko.md',
    required: [
      '명시적인 creation intent를 먼저 commit한다.',
      'message target은 계속 Spot RID다.',
      'Instance Spot queue에는 direct payload와 timer callback만 추가한다.',
      'Missing Instance Spot의 일반 message가 type·Mesh를 새로 제공하거나 creation intent를 만들지 않는다.',
    ],
  },
  {
    path: 'framework/doc/framework/spec/server/24-spot-address-messaging.ko.md',
    required: [
      '`SpotHandle`, 별도 resolver handle과 `InstanceSpotAddress`는 제공하지 않는다.',
      '## 3. Create와 GetOrCreate',
      '일반 send·request는 type이나 Mesh를 입력으로 받지 않으며 Missing RID의 최초 creation intent를 만들지 않는다.',
      'Public `Close`는 exact `SpotRef`를 받는다.',
      'Create·GetOrCreate가 target RID와 endpoint를 application에 요구하지 않는다.',
    ],
  },
  {
    path: 'framework/doc/framework/spec/server/40-location-runtime.ko.md',
    required: [
      'Manager의 명시적인 `Create` 또는 `GetOrCreate`로 생성한다.',
      'User·Instance Spot의 `Create`는 Framework가 SpotRid를 발급하고, `GetOrCreate`는 caller의 SpotRid와 stable type을',
      'creation intent와 target pending capacity를 하나의 atomic',
      '일반 message와 find가 Missing Instance Spot을 hidden create하지 않는다.',
    ],
  },
];

const projections = {
  dotnet: [
    'public readonly record struct SpotRef(',
    'RoutingId SpotRid, ulong ObjectGeneration, string MeshName, RoutingId NodeRid',
    'public interface IZLinkInstanceSpot',
    'public interface IZLinkInstanceSpotHandlerRegistry',
    'IZLinkSendCall SendToSpot<TMessage>(RoutingId spotRid, TMessage message);',
    'IZLinkRequestCall RequestToSpot<TRequest>(RoutingId spotRid, TRequest request);',
    'public interface IZLinkSpotManager',
    'IZLinkSpotCreateCall Create(',
    'IZLinkSpotGetOrCreateCall GetOrCreate(',
    'ValueTask<bool> CloseAsync( SpotRef spot,',
  ],
  cpp: [
    'using spot_rid_t = zlink::routing_id_t;',
    'class spot_ref_t final',
    'std::uint64_t object_generation() const noexcept;',
    'class instance_spot_t {',
    'class instance_spot_handler_registry_t {',
    'send_call_t send_to_spot(spot_rid_t target, TCommand command);',
    'request_call_t<TReply> request_to_spot( spot_rid_t target, TRequest request);',
    'class spot_manager_t {',
    'virtual spot_create_call_t create(',
    'virtual spot_create_call_t get_or_create(',
    'virtual task_t<bool> close(spot_ref_t spot) = 0;',
  ],
  java: [
    'public record SpotRef(',
    'RoutingId spotRid, long objectGeneration, String meshName, RoutingId nodeRid',
    'public interface ZLinkInstanceSpot',
    'public interface ZLinkInstanceSpotHandlerRegistry',
    'ZLinkSpotManager { public abstract systems.zlink.framework.spots.ZLinkSpotCreateCall create(',
    'ZLinkSpotGetOrCreateCall getOrCreate(',
    'close(systems.zlink.framework.spots.SpotRef);',
    'sendToSpot(systems.zlink.contracts.core.RoutingId, java.lang.Object);',
    'requestToSpot(systems.zlink.contracts.core.RoutingId, java.lang.Object);',
  ],
  kotlin: [
    'SpotRid는 Location Store transaction domain 전체에서 유일한 logical ID다.',
    '`SpotRef(spotRid, objectGeneration, meshName, nodeRid)`는 exact incarnation을 close할 때만',
    '`ZLinkSpotManager.create(spotKind, spotType)`은 RID를 생성하고, `getOrCreate(spotRid, spotKind, spotType)`은',
    'Kotlin은 address DTO, process-local handle, resolver, unbounded directory와 direct create/get-or-create terminal extension을 제공하지 않는다.',
    '`close(SpotRef)`는 Missing이면 `false`, generation 불일치는 `SpotGenerationStale`',
  ],
  node: [
    'export type SpotRid = RoutingId;',
    'export interface SpotRef { readonly spotRid: SpotRid; readonly objectGeneration: bigint; readonly meshName: string; readonly nodeRid: RoutingId;',
    'export interface ZLinkInstanceSpot {',
    'export interface ZLinkInstanceSpotHandlerRegistry {',
    'sendToSpot(spotRid: SpotRid, message: unknown): ZLinkSendCall;',
    'requestToSpot(spotRid: SpotRid, request: unknown): ZLinkRequestCall;',
    'export interface ZLinkSpotManager {',
    'create(kind: ZLinkCreatableSpotKind, spotType: string): ZLinkSpotCreateCall;',
    'getOrCreate( spotRid: SpotRid, kind: ZLinkCreatableSpotKind, spotType: string): ZLinkSpotGetOrCreateCall;',
    'close(spot: SpotRef, signal?: AbortSignal): Promise<boolean>;',
  ],
};

const forbiddenRules = [
  {
    label: 'legacy Instance Spot address',
    pattern: /\b(?:InstanceSpotAddress|instance_spot_address_t)\b/u,
    sample: 'public interface InstanceSpotAddress {}',
  },
  {
    label: 'process-local Spot handle',
    pattern: /\b(?:SpotHandle|ZLinkSpotHandle|IZLinkSpotHandle|spot_handle_t)\b/u,
    sample: 'class spot_handle_t {};',
  },
  {
    label: 'public Spot resolver',
    pattern: /\b(?:ZLinkSpotResolver|IZLinkSpotResolver|SpotResolver|spot_resolver_t|resolveSpot|resolve_spot)\b/u,
    sample: 'interface ZLinkSpotResolver {}',
  },
  {
    label: 'local-only Spot lifecycle',
    pattern: /\b(?:CreateLocalSpot|GetOrCreateLocalSpot|createLocalSpot|getOrCreateLocalSpot|create_local_spot|get_or_create_local_spot)\b/u,
    sample: 'createLocalSpot(type);',
  },
  {
    label: 'first-message creation switch',
    pattern: /\b(?:CreateIfMissing|createIfMissing|create_if_missing)\b/u,
    sample: 'sendToSpot(id, message, createIfMissing);',
  },
  {
    label: 'legacy Instance-specific lifecycle operation',
    pattern: /\b(?:CreateInstanceSpot|GetOrCreateInstanceSpot|createInstanceSpot|getOrCreateInstanceSpot|create_instance_spot|get_or_create_instance_spot)\b/u,
    sample: 'createInstanceSpot(type);',
  },
  {
    label: 'target-selecting Spot create',
    pattern: /(?:IZLinkSpotCreateCall\s+Create|ZLinkSpotCreateCall\s+create|spot_create_call_t\s+create)\s*\([^)]*\b(?:NodeRid|nodeRid|node_rid|targetNode|target_node|endpoint)\b[^)]*\)/su,
    sample: 'ZLinkSpotCreateCall create(NodeRid targetNode, String type);',
  },
];

const contracts = new Map(languages.map(language => [
  language,
  readExactContract(root, language, tags[language]),
]));

const missingProjectionFailures = (language, source) => projections[language]
  .filter(fragment => !normalized(source).includes(normalized(fragment)))
  .map(fragment => `${language} exact interface is missing "${normalized(fragment)}"`);
const forbiddenContractFailures = (language, code) => forbiddenRules
  .filter(rule => rule.pattern.test(code))
  .map(rule => `${language} exact interface exposes ${rule.label}`);

const failures = [];
for (const fixture of formalFixtures) {
  const source = normalized(read(fixture.path));
  for (const fragment of fixture.required) {
    if (!source.includes(normalized(fragment))) {
      failures.push(`formal contract is missing "${normalized(fragment)}": ${fixture.path}`);
    }
  }
}

for (const language of languages) {
  const contract = contracts.get(language);
  failures.push(...missingProjectionFailures(language, contract.source));
  failures.push(...forbiddenContractFailures(language, contract.code));
}

const actorFreeRules = [
  ['dotnet', /interface\s+IZLinkInstanceSpot\s*:\s*IZLinkSpot\b/su],
  ['cpp', /class\s+instance_spot_t\s*:\s*public\s+spot_t\b/su],
  ['java', /interface\s+ZLinkInstanceSpot\s+extends\s+ZLinkSpot\b/su],
  ['node', /interface\s+ZLinkInstanceSpot\s+extends\s+ZLinkSpot\b/su],
];
for (const [language, pattern] of actorFreeRules) {
  if (pattern.test(contracts.get(language).code)) {
    failures.push(`${language} Instance Spot lifecycle inherits the actor-capable Spot interface`);
  }
}

if (failures.length > 0) {
  process.stderr.write(`${failures.map(message => `FAIL: ${message}`).join('\n')}\n`);
  process.exit(1);
}

let negativeMutations = 0;
if (mode === '--self-test') {
  for (const rule of forbiddenRules) {
    const rejected = forbiddenContractFailures('negative', rule.sample)
      .some(failure => failure.includes(rule.label));
    if (!rejected) {
      throw new Error(`negative self-test did not reject ${rule.label}`);
    }
    negativeMutations += 1;
  }
  for (const language of languages) {
    const fragment = normalized(projections[language][0]);
    const source = normalized(contracts.get(language).source);
    const mutated = source.split(fragment).join('');
    if (!missingProjectionFailures(language, mutated)
      .some(failure => failure.includes(fragment))) {
      throw new Error(`negative self-test did not reject missing ${language} required projection`);
    }
    negativeMutations += 1;
  }
}

process.stdout.write(
  `INSTANCE SPOT CONTRACTS CLEAN languages=${languages.length}`
  + ` formal_documents=${formalFixtures.length}`
  + ` required_fragments=${Object.values(projections).flat().length}`
  + ` forbidden_rules=${forbiddenRules.length}`
  + ` negative_mutations=${negativeMutations}\n`);
NODE
