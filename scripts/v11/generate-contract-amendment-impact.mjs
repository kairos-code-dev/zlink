#!/usr/bin/env node

// SPDX-License-Identifier: MPL-2.0

import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(scriptDirectory, '../..');
const manifestPath = path.join(
  root,
  'framework/doc/plan/v11.0/route-mesh-11.0.0-contract-amendment-impact.json',
);

const git = (args, encoding = 'utf8') => {
  const result = spawnSync('git', args, {cwd: root, encoding});
  if (result.status !== 0) {
    throw new Error(`git ${args.join(' ')} failed: ${String(result.stderr).trim()}`);
  }
  return result.stdout;
};

const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
const stableJson = value => `${JSON.stringify(value, null, 2)}\n`;
const baselineRevision = git(['rev-parse', 'HEAD']).trim();

function baselineFiles(relative) {
  const output = git(['ls-tree', '-r', '--full-tree', baselineRevision, '--', relative]);
  return output.trim().length === 0 ? [] : output.trim().split('\n').map(line => {
    const match = /^(\d+) blob ([0-9a-f]+)\t(.+)$/u.exec(line);
    if (!match) throw new Error(`unexpected git ls-tree record: ${line}`);
    return {object: match[2], path: match[3]};
  });
}

function baselineHash(relative) {
  const records = baselineFiles(relative).map(record => ({
    path: record.path,
    blob: record.object,
  })).sort((left, right) => left.path.localeCompare(right.path, 'en'));
  if (records.length === 0) throw new Error(`baseline path is empty: ${relative}`);
  return sha256(stableJson(records));
}

function directDirectories(relative) {
  const prefix = `${relative}/`;
  return [...new Set(baselineFiles(relative).map(record => {
    const remainder = record.path.slice(prefix.length);
    return remainder.split('/')[0];
  }).filter(Boolean))].sort((left, right) => left.localeCompare(right, 'en'));
}

function activationStage(name) {
  const normalized = name.toLowerCase();
  if (/observability|monitoring|storefailure|store-failure|resilience/u.test(normalized)) {
    return 'V11-M6C-E2E';
  }
  if (/spot|actor|instance|automaticturn|automatic-turn|yielddispatch|toactormessaging/u.test(normalized)) {
    return 'V11-M6B-E2E';
  }
  return 'V11-M6A-E2E';
}

function languageOwner(language, stage) {
  const suffix = {cpp: 'CPP', dotnet: 'DN', java: 'JVM', kotlin: 'JVM', node: 'NODE'}[language];
  if (stage === 'V11-M6A-E2E') return `V11-M6A-${suffix}`;
  if (stage === 'V11-M6B-E2E') return `V11-M6B-${suffix}`;
  return `V11-M6C-${suffix}`;
}

function registrationFiles(relative) {
  return baselineFiles(relative).filter(record => {
    const name = path.basename(record.path);
    return /^(?:run|verify|test)[-_].*\.(?:sh|ps1|mjs|js)$/u.test(name)
      || name === 'CMakeLists.txt'
      || name === 'package.json'
      || name === 'pom.xml'
      || /\.csproj$/u.test(name)
      || /^build\.gradle(?:\.kts)?$/u.test(name);
  });
}

const entries = [];
const addBaselineEntry = entry => entries.push({
  ...entry,
  baselineHash: baselineHash(entry.path),
  approvedHash: null,
});

const languageRoots = [
  {language: 'cpp', e2e: ['framework/languages/cpp/e2e'], samples: ['framework/languages/cpp/samples']},
  {language: 'dotnet', e2e: ['framework/languages/dotnet/e2e'], samples: ['framework/languages/dotnet/samples']},
  {
    language: 'java',
    e2e: ['framework/languages/java/e2e'],
    samples: ['framework/languages/java/samples/java'],
  },
  {
    language: 'kotlin',
    e2e: ['framework/languages/java/e2e-kotlin'],
    samples: ['framework/languages/java/samples/kotlin'],
  },
  {language: 'node', e2e: ['framework/languages/node/e2e'], samples: ['framework/languages/node/samples']},
];

for (const scope of languageRoots) {
  for (const e2eRoot of scope.e2e) {
    if (baselineFiles(e2eRoot).length === 0) continue;
    for (const suite of directDirectories(e2eRoot)) {
      const stage = activationStage(suite);
      const suitePath = `${e2eRoot}/${suite}`;
      addBaselineEntry({
        id: `e2e:${scope.language}:${suite}`,
        kind: 'e2e-scenario-suite',
        language: scope.language,
        path: suitePath,
        disposition: 'amend',
        acceptanceIntent: '기존 scenario의 검증 의미를 유지하면서 global identity, explicit create와 Framework runtime 경로에 맞춘다.',
        replacementCoverage: [],
        specOwner: 'framework/doc/framework/common/e2e/README.ko.md',
        runtimeOwner: languageOwner(scope.language, stage),
        activationStage: stage,
        quarantineStatus: 'pending-disabled-by-contract-amendment',
      });
      for (const registration of registrationFiles(suitePath)) {
        addBaselineEntry({
          id: `registration:e2e:${scope.language}:${suite}:${sha256(registration.path).slice(0, 12)}`,
          kind: 'e2e-registration',
          language: scope.language,
          path: registration.path,
          disposition: 'retain',
          acceptanceIntent: 'source와 registration을 보존하고 승인된 activation stage 전에는 실행 graph에 넣지 않는다.',
          replacementCoverage: [],
          specOwner: 'framework/doc/framework/common/e2e/README.ko.md',
          runtimeOwner: languageOwner(scope.language, stage),
          activationStage: stage,
          quarantineStatus: 'pending-disabled-by-contract-amendment',
        });
      }
    }
  }
  for (const sampleRoot of scope.samples) {
    if (baselineFiles(sampleRoot).length === 0) continue;
    for (const sample of directDirectories(sampleRoot)) {
      const samplePath = `${sampleRoot}/${sample}`;
      addBaselineEntry({
        id: `sample:${scope.language}:${sample}`,
        kind: 'sample',
        language: scope.language,
        path: samplePath,
        disposition: 'amend',
        acceptanceIntent: '기존 사용자 흐름과 marker를 유지하면서 amended public contract만 사용한다.',
        replacementCoverage: [],
        specOwner: 'framework/doc/framework/common/sample/README.ko.md',
        runtimeOwner: languageOwner(scope.language, 'V11-M6B-E2E'),
        activationStage: 'V11-M7-SAMPLES',
        quarantineStatus: 'pending-disabled-by-contract-amendment',
      });
      for (const registration of registrationFiles(samplePath)) {
        addBaselineEntry({
          id: `registration:sample:${scope.language}:${sample}:${sha256(registration.path).slice(0, 12)}`,
          kind: 'sample-registration',
          language: scope.language,
          path: registration.path,
          disposition: 'retain',
          acceptanceIntent: 'sample source와 runner registration을 보존하고 V11-M7-SAMPLES에서만 활성화한다.',
          replacementCoverage: [],
          specOwner: 'framework/doc/framework/common/sample/README.ko.md',
          runtimeOwner: languageOwner(scope.language, 'V11-M6B-E2E'),
          activationStage: 'V11-M7-SAMPLES',
          quarantineStatus: 'pending-disabled-by-contract-amendment',
        });
      }
    }
  }
}

for (const documentRoot of [
  {path: 'framework/doc/framework/common/e2e', kind: 'e2e-contract', activationStage: 'V11-E2E-SPEC-FINAL'},
  {path: 'framework/doc/framework/common/sample', kind: 'sample-contract', activationStage: 'V11-SAMPLE-SPEC-FINAL'},
]) {
  for (const record of baselineFiles(documentRoot.path).filter(item => item.path.endsWith('.ko.md'))) {
    addBaselineEntry({
      id: `${documentRoot.kind}:${path.basename(record.path, '.ko.md')}:${sha256(record.path).slice(0, 12)}`,
      kind: documentRoot.kind,
      language: 'common',
      path: record.path,
      disposition: 'amend',
      acceptanceIntent: '통합된 formal contract와 runtime 검증 순서에 맞춰 scenario 또는 sample acceptance를 확정한다.',
      replacementCoverage: [],
      specOwner: record.path,
      runtimeOwner: 'V11-M6-SCAFFOLD-ZERO',
      activationStage: documentRoot.activationStage,
      quarantineStatus: 'pending-disabled-by-contract-amendment',
    });
  }
}

const regressionRoots = [
  ['cpp', 'framework/languages/cpp/tests'],
  ['dotnet', 'framework/languages/dotnet/tests'],
  ['java', 'framework/languages/java/zlink-framework-core/src/test'],
  ['java', 'framework/languages/java/zlink-framework-spring-boot-starter/src/test'],
  ['java', 'framework/languages/java/zlink-framework-locations-redis/src/test'],
  ['kotlin', 'framework/languages/java/zlink-framework-kotlin/src/test'],
  ['node', 'framework/languages/node/test'],
  ['common', 'framework/runtime/protocol'],
];
for (const [language, regressionPath] of regressionRoots) {
  if (baselineFiles(regressionPath).length === 0) continue;
  addBaselineEntry({
    id: `regression:${language}:${path.basename(regressionPath)}:${sha256(regressionPath).slice(0, 12)}`,
    kind: 'regression-root',
    language,
    path: regressionPath,
    disposition: 'retain',
    acceptanceIntent: 'runtime 구현 중 계속 실행하며 ordering, terminal winner, ownership, CAS, lease, fencing과 cleanup assertion을 약화하지 않는다.',
    replacementCoverage: [],
    specOwner: language === 'common'
      ? 'framework/doc/framework/common/internals/README.ko.md'
      : `framework/doc/framework/spec/server/languages/${language}/interfaces/README.ko.md`,
    runtimeOwner: language === 'common' ? 'V11-CA-PROTOCOL' : languageOwner(language, 'V11-M6A-E2E'),
    activationStage: 'V11-M6-SCAFFOLD-ZERO',
    quarantineStatus: 'active-regression',
  });
}

const plannedAdds = [
  ['e2e:add:global-actor-remote-create', 'e2e-scenario', 'global ActorId concurrent Create와 remote placement가 authority 하나로 수렴한다.', 'V11-M6B-E2E'],
  ['e2e:add:global-spot-explicit-create', 'e2e-scenario', 'global SpotRid explicit Create와 stored intent reactivation을 검증한다.', 'V11-M6B-E2E'],
  ['e2e:add:exact-generation-mutation-bind', 'e2e-scenario', 'stale generation의 destroy, close와 session bind가 새 incarnation에 적용되지 않는다.', 'V11-M6B-E2E'],
  ['e2e:add:placement-capacity-weight-affinity', 'e2e-scenario', 'role, capability, profile, affinity, weight와 active·pending capacity 순서를 검증한다.', 'V11-M6A-E2E'],
  ['e2e:add:automatic-rid-collision', 'e2e-scenario', '128-bit random RID 충돌 8회 뒤 startup이 RoutingIdConflict로 끝난다.', 'V11-M6A-E2E'],
  ['e2e:add:reservation-crash-recovery', 'e2e-scenario', 'generic Reserve·Commit·Abort와 owner lease takeover가 pending capacity를 exact fence로 회수한다.', 'V11-M6C-E2E'],
  ['e2e:add:forwarding-bounds', 'e2e-scenario', 'forwarding 8 hops, 1024 messages와 16 MiB bound에서 typed terminal 결과를 검증한다.', 'V11-M6B-E2E'],
  ['sample:add:remote-object-create', 'sample-contract', 'Client와 Server role을 분리한 remote Actor·Spot create 사용 흐름을 제공한다.', 'V11-M7-SAMPLES'],
  ['regression:add:no-negative-route-cache', 'regression', 'Missing, Creating과 Store failure가 negative cache에 남지 않는지 검증한다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['regression:add:global-authority-key', 'regression', 'MeshName과 독립적인 ActorId·SpotRid authority key encoding을 네 runtime에서 검증한다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['e2e:add:transfer-store-required-registration', 'e2e-scenario', 'Recreate 또는 Snapshot policy를 하나라도 등록한 Object Server가 Transfer Store를 정확히 하나 등록하고 socket bind 전에 검증을 통과한다.', 'V11-M6C-E2E'],
  ['e2e:add:transfer-store-registration-bind-failure', 'e2e-scenario', 'Recreate 또는 Snapshot policy에서 Transfer Store가 없거나 중복 등록되면 socket bind 전에 startup configuration error로 종료한다.', 'V11-M6C-E2E'],
  ['e2e:add:disabled-only-without-transfer-store', 'e2e-scenario', '모든 object policy가 Disabled인 Object Server는 Transfer Store 없이 시작하며 cross-node 이동을 Capture 전에 거부한다.', 'V11-M6C-E2E'],
  ['e2e:add:same-node-join-without-transfer-payload', 'e2e-scenario', 'same-node Actor join은 Transfer Store에 payload를 쓰거나 Location Store에 transfer reference를 publish하지 않는다.', 'V11-M6B-E2E'],
  ['e2e:add:redis-stores-shared-deployment', 'e2e-scenario', '공식 Redis Location Store와 Redis Transfer Store가 같은 Redis deployment를 사용하더라도 서로 다른 key prefix와 분리된 구현 class를 유지한다.', 'V11-M6C-E2E'],
  ['e2e:add:redis-stores-separate-deployments', 'e2e-scenario', 'Location Store와 Transfer Store를 물리적으로 다른 Redis deployment에 배치해도 cross-node 이동의 visibility와 recovery 의미가 동일하다.', 'V11-M6C-E2E'],
  ['e2e:add:published-transfer-data-lost', 'e2e-scenario', 'Location Store가 publish한 transfer reference의 payload가 영구적으로 없거나 checksum 또는 inventory digest가 맞지 않으면 TransferDataLost로 종료하고 이전 owner로 rollback하지 않는다.', 'V11-M6C-E2E'],
  ['regression:add:transfer-write-before-location-cas', 'regression', 'immutable transfer root와 reference·checksum·retention 검증이 끝나기 전에 Location Store authority CAS가 실행되지 않는지 검증한다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['regression:add:transfer-cas-conflict-orphan-cleanup', 'regression', 'Transfer Store 저장 뒤 Location CAS가 충돌하면 authority를 변경하지 않고 미공개 root를 orphan TTL 또는 idempotent delete 대상으로 남긴다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['regression:add:transfer-root-replacement-order', 'regression', '새 immutable transfer root 저장, Location reference CAS, 이전 root 정리 순서를 고정하고 중간 실패가 published root를 손상하지 않는지 검증한다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['regression:add:transfer-reference-release-before-delete', 'regression', 'Location Store가 transfer reference 사용 종료를 CAS한 뒤에만 Transfer Store payload를 삭제한다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['regression:add:location-participant-digest-authority', 'regression', 'bounded canonical participant set과 inventory digest의 authority는 Location Store에만 있고 Transfer manifest는 payload 탐색에만 사용하며 두 digest가 일치해야 한다.', 'V11-M6-SCAFFOLD-ZERO'],
  ['regression:add:transfer-data-lost-no-rollback', 'regression', 'published payload의 영구 누락, checksum 불일치와 participant inventory digest 불일치를 non-retriable TransferDataLost로 분류하고 임의 rollback을 금지한다.', 'V11-M6-SCAFFOLD-ZERO'],
];
for (const [id, kind, acceptanceIntent, activationStage] of plannedAdds) {
  entries.push({
    id,
    kind,
    language: 'common',
    path: null,
    disposition: 'add',
    baselineHash: null,
    approvedHash: null,
    acceptanceIntent,
    replacementCoverage: [],
    specOwner: id.includes('transfer-store')
        || id.includes('transfer-')
        || id.includes('redis-stores')
        || id.includes('location-participant-digest')
      ? 'framework/doc/framework/spec/server/42-transfer-store-redis.ko.md'
      : kind.startsWith('sample')
        ? 'framework/doc/framework/common/sample/README.ko.md'
        : 'framework/doc/framework/common/e2e/README.ko.md',
    runtimeOwner: activationStage.startsWith('V11-M6C') ? 'V11-M6C-CPP'
      : activationStage.startsWith('V11-M6B') ? 'V11-M6B-CPP'
        : 'V11-M6A-CPP',
    activationStage,
    quarantineStatus: kind === 'regression' ? 'planned-regression' : 'pending-disabled-by-contract-amendment',
  });
}

entries.sort((left, right) => left.id.localeCompare(right.id, 'en'));
const manifest = {
  schemaVersion: 1,
  version: '11.0.0',
  baselineRevision,
  contractDecisionRange: 'CA-D01..CA-D36',
  state: 'pending-disabled-by-contract-amendment',
  execution: {executed: 0, skipped: 0},
  entries,
};

const command = process.argv[2];
if (!['--write', '--check'].includes(command)) {
  process.stderr.write('usage: generate-contract-amendment-impact.mjs --write|--check\n');
  process.exit(2);
}
const output = stableJson(manifest);
if (command === '--write') {
  fs.writeFileSync(manifestPath, output);
  process.stdout.write(`wrote ${path.relative(root, manifestPath)}: entries=${entries.length}\n`);
} else {
  if (!fs.existsSync(manifestPath) || fs.readFileSync(manifestPath, 'utf8') !== output) {
    throw new Error('contract amendment impact manifest is stale; run --write after reviewing baseline scope');
  }
  process.stdout.write(`verified ${path.relative(root, manifestPath)}: entries=${entries.length}\n`);
}
