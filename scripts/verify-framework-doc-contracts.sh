#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
inventory="$repo_root/framework/doc/contract-inventory/route-mesh-v10-contract-inventory.json"
dotnet_inventory="$repo_root/framework/doc/contract-inventory/route-mesh-v10-dotnet-contract-inventory.json"

node - "$repo_root" "$inventory" "$dotnet_inventory" <<'NODE'
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');

const root = process.argv[2];
const inventory = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const dotnetInventory = JSON.parse(fs.readFileSync(process.argv[4], 'utf8'));
const failures = [];

const read = relative => {
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    failures.push(`missing file: ${relative}`);
    return undefined;
  }
  return fs.readFileSync(absolute, 'utf8');
};

const digest = value => crypto.createHash('sha256').update(value).digest('hex');
const languages = ['dotnet', 'cpp', 'java', 'kotlin', 'node'];
const codeTags = {
  dotnet: ['csharp'],
  cpp: ['cpp'],
  java: ['java'],
  kotlin: ['kotlin'],
  node: ['ts', 'typescript']
};

const declarationNames = (language, source) => {
  const names = [];
  const add = pattern => {
    let match;
    while ((match = pattern.exec(source)) !== null) names.push(match[1]);
  };
  if (language === 'dotnet') {
    add(/public\s+(?:(?:sealed|abstract|readonly)\s+)*(?:class|interface|enum|record(?:\s+struct)?)\s+([A-Za-z_]\w*)/g);
    add(/public\s+delegate\s+[^;(]*?\s+([A-Za-z_]\w*)\s*\(/g);
  } else if (language === 'cpp') {
    add(/\b(?:class|struct|enum\s+class|enum)\s+([A-Za-z_]\w*)/g);
    add(/\busing\s+([A-Za-z_]\w*)\s*=/g);
  } else if (language === 'java') {
    add(/public\s+(?:sealed\s+|final\s+|abstract\s+)?(?:class|interface|record|enum|@interface)\s+([A-Za-z_]\w*)/g);
  } else if (language === 'kotlin') {
    add(/\b(?:data\s+class|sealed\s+interface|sealed\s+class|enum\s+class|class|interface|object|typealias)\s+([A-Za-z_]\w*)/g);
    add(/\bfun\s+(?:<[^>]+>\s*)?(?:[A-Za-z_][\w.<>?]*\.)?([A-Za-z_]\w*)\s*\(/g);
  } else if (language === 'node') {
    add(/\bexport\s+(?:declare\s+)?(?:interface|class|enum|type|const|function)\s+([A-Za-z_]\w*)/g);
  }
  return names;
};
if (inventory.schema !== 2 || inventory.version !== '10.0.0') {
  failures.push('unified inventory must use schema 2 and version 10.0.0');
}
if (JSON.stringify(Object.keys(inventory.languages).sort()) !== JSON.stringify([...languages].sort())) {
  failures.push('unified inventory must contain dotnet, cpp, java, kotlin and node');
}

const exactDocuments = new Set();
let codeFixtureCount = 0;
let declarationCount = 0;
for (const language of languages) {
  const projection = inventory.languages[language];
  if (!projection || !Array.isArray(projection.documents) || projection.documents.length === 0) {
    failures.push(`missing document projection: ${language}`);
    continue;
  }
  const combined = [];
  let codeFixtureSource = '';
  for (const relative of projection.documents) {
    exactDocuments.add(relative);
    const source = read(relative);
    if (source !== undefined) {
      combined.push(source);
      const blocks = [];
      for (const tag of codeTags[language]) {
        const pattern = new RegExp('```' + tag + '\\n([\\s\\S]*?)```', 'g');
        for (const match of source.matchAll(pattern)) {
          blocks.push(match[1].replace(/[ \t]+$/gm, '').trim());
        }
      }
      if (blocks.length > 0) {
        codeFixtureCount += 1;
        const normalized = blocks.join('\n---BLOCK---\n');
        codeFixtureSource += normalized + '\n';
        const fixture = projection.code_fixtures?.[relative];
        if (!fixture || fixture.block_count !== blocks.length || fixture.sha256 !== digest(normalized)) {
          failures.push(`code fixture differs: ${language}: ${relative}`);
        }
      } else if (projection.code_fixtures?.[relative]) {
        failures.push(`declared code fixture has no blocks: ${language}: ${relative}`);
      }
    }
  }
  const contract = combined.join('\n');
  for (const fragment of projection.required_fragments || []) {
    if (!contract.includes(fragment)) failures.push(`missing ${language} required fragment: ${fragment}`);
  }
  for (const fragment of inventory.forbidden_exact_fragments?.[language] || []) {
    if (contract.includes(fragment)) failures.push(`forbidden ${language} exact fragment: ${fragment}`);
  }
  for (const relative of Object.keys(projection.code_fixtures || {})) {
    if (!projection.documents.includes(relative)) failures.push(`code fixture is outside ${language} document set: ${relative}`);
  }
  const counts = new Map();
  for (const name of declarationNames(language, codeFixtureSource)) counts.set(name, (counts.get(name) || 0) + 1);
  const canonical = [...counts].sort(([left], [right]) => left < right ? -1 : left > right ? 1 : 0)
    .map(([name, count]) => `${name}=${count}`).join('\n');
  if (!projection.declaration_fixture
      || projection.declaration_fixture.count !== counts.size
      || projection.declaration_fixture.sha256 !== digest(canonical)) {
    failures.push(`public declaration inventory differs: ${language}`);
  }
  declarationCount += counts.size;
}

const connectorLanguages = ['dotnet', 'cpp', 'java', 'typescript'];
const connectorInventory = inventory.stream_connector_exact;
const streamConnectorDocuments = new Set();
if (!connectorInventory
    || typeof connectorInventory.common_document !== 'string'
    || JSON.stringify(Object.keys(connectorInventory.languages || {}).sort())
      !== JSON.stringify([...connectorLanguages].sort())) {
  failures.push('stream connector exact inventory must contain common, dotnet, cpp, java and typescript contracts');
} else {
  streamConnectorDocuments.add(connectorInventory.common_document);
  read(connectorInventory.common_document);
  for (const language of connectorLanguages) {
    const projection = connectorInventory.languages[language];
    streamConnectorDocuments.add(projection.document);
    const source = read(projection.document);
    if (source === undefined) continue;
    for (const fragment of projection.required_fragments || []) {
      if (!source.includes(fragment)) failures.push(`missing ${language} stream connector fragment: ${fragment}`);
    }
    for (const fragment of projection.forbidden_fragments || []) {
      if (source.includes(fragment)) failures.push(`forbidden ${language} stream connector fragment: ${fragment}`);
    }
    const blocks = [];
    for (const tag of projection.code_tags || []) {
      const pattern = new RegExp('```' + tag + '\\n([\\s\\S]*?)```', 'g');
      for (const match of source.matchAll(pattern)) {
        blocks.push(match[1].replace(/[ \t]+$/gm, '').trim());
      }
    }
    const fixture = projection.code_fixture;
    const normalized = blocks.join('\n---BLOCK---\n');
    if (!fixture || fixture.block_count !== blocks.length || fixture.sha256 !== digest(normalized)) {
      failures.push(`stream connector code fixture differs: ${language}: ${projection.document}`);
    }
  }
}

// Handshake failure belongs to socket runtime monitoring, not to a STREAM
// session error callback. Keep the six-value monitoring surface aligned in
// every language projection; Kotlin intentionally reuses the Java surface.
const enumMembers = (relative, type) => {
  const source = read(relative);
  if (source === undefined) return undefined;
  const escaped = type.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const declaration = new RegExp(`\\benum(?:\\s+class)?\\s+${escaped}\\b[^\\{]*\\{([\\s\\S]*?)\\}`, 'm').exec(source);
  if (!declaration) {
    failures.push(`runtime monitoring enum is absent: ${relative}: ${type}`);
    return undefined;
  }
  return declaration[1]
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/.*$/gm, '')
    .split(',')
    .map(member => /^\s*([A-Za-z_]\w*)/.exec(member)?.[1])
    .filter(Boolean);
};
const runtimeMonitorEnums = [
  {
    language: 'dotnet',
    document: 'framework/doc/framework/spec/server/languages/dotnet/02-handler-interfaces.ko.md',
    type: 'ZLinkSocketEventKind',
    members: ['Connected', 'ConnectionReady', 'Disconnected', 'HandshakeFailed', 'PeerAdmissionChanged', 'Closed']
  },
  {
    language: 'cpp',
    document: 'framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md',
    type: 'socket_event_kind_t',
    members: ['connected', 'connection_ready', 'disconnected', 'handshake_failed', 'peer_admission_changed', 'closed']
  },
  {
    language: 'java',
    document: 'framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md',
    type: 'ZLinkSocketEventKind',
    members: ['CONNECTED', 'CONNECTION_READY', 'DISCONNECTED', 'HANDSHAKE_FAILED', 'PEER_ADMISSION_CHANGED', 'CLOSED']
  },
  {
    language: 'node',
    document: 'framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md',
    type: 'ZLinkSocketEventKind',
    members: ['Connected', 'ConnectionReady', 'Disconnected', 'HandshakeFailed', 'PeerAdmissionChanged', 'Closed']
  }
];
for (const surface of runtimeMonitorEnums) {
  const actual = enumMembers(surface.document, surface.type);
  if (actual !== undefined && JSON.stringify(actual) !== JSON.stringify(surface.members)) {
    failures.push(`runtime monitoring surface differs: ${surface.language}.${surface.type}`);
  }
}
const kotlinContract = read('framework/doc/framework/spec/server/languages/kotlin/README.ko.md');
const kotlinRuntime = read('framework/doc/framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md');
if (kotlinContract !== undefined && !kotlinContract.includes('Java 공개 계약')) {
  failures.push('Kotlin exact interface must identify the reused Java public contract');
}
if (kotlinRuntime !== undefined
    && !(kotlinRuntime.includes('Java 공개 계약') && kotlinRuntime.includes('Java 타입을 복사해 다시 정의하지 않는다'))) {
  failures.push('Kotlin runtime monitoring surface must explicitly reuse Java types');
}

// Message flow, dispatch failure and observer-failure reporting have one
// common owner. Lock the closed values and each language's public observer
// and runtime-error sink projection so an internal helper cannot satisfy the
// public Config 5 contract.
const flowOwner = read('framework/doc/framework/spec/server/52-message-flow-tracing.ko.md');
const requiredFlowOwnerFragments = [
  'zlink.message_flow', 'zlink.dispatch_error', 'zlink.runtime_error',
  '`succeeded`, `failed`, `backpressured`, `dropped`, `cancelled`, `shutdown`',
  '`no_handler`, `decode_error`, `handler_exception`, `invalid_frame`, `reply_path_missing`',
  '`reply_error`, `fail_caller`, `drop`',
  '`observer_failed`', '`message_flow_observer`'
];
for (const fragment of requiredFlowOwnerFragments) {
  if (flowOwner !== undefined && !flowOwner.includes(fragment)) {
    failures.push(`message-flow owner fragment is absent: ${fragment}`);
  }
}

const flowExactProjections = [
  {
    language: 'dotnet',
    document: 'framework/doc/framework/spec/server/languages/dotnet/05-route-mesh.ko.md',
    required: ['IZLinkMessageFlowObserver', 'IZLinkRuntimeErrorSink', 'ZLinkRuntimeErrorEvent',
      'SetRuntimeErrorSink', 'string? Action', 'zlink.runtime_error', 'observer_failed', 'message_flow_observer'],
    forbidden: []
  },
  {
    language: 'cpp',
    document: 'framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md',
    required: ['message_flow_observer_t', 'runtime_error_sink_t', 'runtime_error_event_t',
      'set_runtime_error_sink', 'std::optional<std::string> action', 'zlink.runtime_error',
      'observer_failed', 'message_flow_observer'],
    forbidden: ['message_dispatch_error_event_t', 'std::exception_ptr', 'outcome=error']
  },
  {
    language: 'java',
    document: 'framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md',
    required: ['ZLinkMessageFlowObserver', 'ZLinkRuntimeErrorSink', 'ZLinkRuntimeErrorEvent',
      'setRuntimeErrorSink', 'Optional<String> action', 'zlink.runtime_error',
      'observer_failed', 'message_flow_observer'],
    forbidden: ['outcome=ERROR', 'exception()']
  },
  {
    language: 'kotlin',
    document: 'framework/doc/framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md',
    required: ['ZLinkRuntimeErrorSink', 'ZLinkRuntimeErrorEvent', 'onRuntimeError',
      'observer_failed', 'message_flow_observer'],
    forbidden: []
  },
  {
    language: 'node',
    document: 'framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md',
    required: ['ZLinkMessageFlowObserver', 'ZLinkRuntimeErrorSink', 'ZLinkRuntimeErrorEvent',
      'setRuntimeErrorSink', '"no_handler"', '"reply_error"', '"observer_failed"',
      '"message_flow_observer"'],
    forbidden: ['ZLinkDispatchFailure', 'errorReason?:', 'errorAction?:', 'errorType?:', 'errorMessage?:']
  }
];
for (const projection of flowExactProjections) {
  const source = read(projection.document);
  if (source === undefined) continue;
  for (const fragment of projection.required) {
    if (!source.includes(fragment)) failures.push(`message-flow ${projection.language} fragment is absent: ${fragment}`);
  }
  for (const fragment of projection.forbidden) {
    if (source.includes(fragment)) failures.push(`message-flow ${projection.language} stale fragment remains: ${fragment}`);
  }
}

const resilienceDocuments = [
  'framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md',
  'framework/languages/dotnet/e2e/ResilienceLifecycle/feature-map.ko.md',
  'framework/languages/cpp/e2e/ResilienceLifecycle/feature-map.ko.md',
  'framework/languages/java/e2e/ResilienceLifecycle/feature-map.ko.md',
  'framework/languages/java/e2e-kotlin/ResilienceLifecycle/feature-map.ko.md',
  'framework/languages/node/e2e/ResilienceLifecycle/feature-map.ko.md'
];
for (const relative of resilienceDocuments) {
  const source = read(relative);
  if (source === undefined) continue;
  for (const fragment of ['zlink.runtime_error', 'observer_failed', 'message_flow_observer', 'no_handler', 'reply_error']) {
    if (!source.includes(fragment)) failures.push(`ResilienceLifecycle public evidence differs: ${relative}: ${fragment}`);
  }
  for (const fragment of ['UnhandledCallbackException', 'reportRuntimeTaskException', 'onRuntimeTaskException']) {
    if (source.includes(fragment)) failures.push(`ResilienceLifecycle uses an internal runtime-error helper: ${relative}: ${fragment}`);
  }
}

for (const [relative, expected] of Object.entries(inventory.source_fixtures || {})) {
  const source = read(relative);
  if (source !== undefined && digest(source) !== expected) {
    failures.push(`transition source fixture changed without inventory review: ${relative}`);
  }
}

const actions = new Set(['keep', 'move', 'remove']);
let transitionMemberCount = 0;
const stripSourceComments = source => source
  .replace(/\/\*[\s\S]*?\*\//g, '')
  .replace(/\/\/.*$/gm, '');

const extractTypeMembers = (source, type) => {
  const clean = stripSourceComments(source);
  const escaped = type.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const declaration = new RegExp(`\\b(?:class|interface)\\s+${escaped}\\b[^\\{]*\\{`, 'm').exec(clean);
  if (!declaration) return undefined;
  const open = declaration.index + declaration[0].lastIndexOf('{');
  let depth = 1;
  let start = open + 1;
  const members = new Set();
  const consume = end => {
    let statement = clean.slice(start, end).trim();
    statement = statement.replace(/^(?:public|private|protected|internal)\s*:\s*/, '').trim();
    if (!statement || /^(?:public|private|protected|internal)\s*:$/.test(statement)) return;
    const methods = [...statement.matchAll(/([~A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^;{}()]*>)?\s*\(/g)];
    if (methods.length > 0) {
      const name = methods[methods.length - 1][1].replace(/^~/, '');
      if (!['if', 'for', 'while', 'switch', 'return', 'requires'].includes(name)) members.add(name);
      return;
    }
    const property = /([A-Za-z_][A-Za-z0-9_]*)\s*(?:\?|!)?\s*(?::[^;{}]+)?$/.exec(statement);
    if (property) members.add(property[1]);
  };
  for (let index = open + 1; index < clean.length && depth > 0; index += 1) {
    const char = clean[index];
    if (char === '{') {
      if (depth === 1) consume(index);
      depth += 1;
    } else if (char === '}') {
      depth -= 1;
      if (depth === 1) start = index + 1;
    } else if (char === ';' && depth === 1) {
      consume(index);
      start = index + 1;
    }
  }
  return members;
};

const extractTopLevelFunctions = source => {
  const members = new Set();
  const clean = stripSourceComments(source);
  const pattern = /\bfun\s+(?:<[^>]+>\s*)?(?:[A-Za-z_][\w.<>?]*\.)?([A-Za-z_]\w*)\s*\(/g;
  let match;
  while ((match = pattern.exec(clean)) !== null) members.add(match[1]);
  return members;
};

for (const language of languages) {
  const owners = inventory.transition_owners?.[language];
  if (!Array.isArray(owners) || owners.length === 0) {
    failures.push(`missing transition owners: ${language}`);
    continue;
  }
  const ownerTypes = new Set();
  for (const owner of owners) {
    if (ownerTypes.has(owner.type)) failures.push(`duplicate transition owner: ${language}.${owner.type}`);
    ownerTypes.add(owner.type);
    if (!inventory.source_fixtures[owner.source]) {
      failures.push(`transition owner source is not hash-locked: ${language}.${owner.type}`);
    }
    const ownerSource = read(owner.source);
    const actualMembers = ownerSource === undefined ? undefined
      : owner.kind === 'top_level'
        ? extractTopLevelFunctions(ownerSource)
        : extractTypeMembers(ownerSource, owner.type);
    if (actualMembers === undefined) {
      failures.push(`transition owner type is absent from source: ${language}.${owner.type}`);
      continue;
    }
    transitionMemberCount += actualMembers.size;
    const rules = owner.rules || [];
    const defaults = rules.filter(rule => rule.default === true);
    if (defaults.length !== 1 || rules[rules.length - 1]?.default !== true) {
      failures.push(`transition owner requires one final default rule: ${language}.${owner.type}`);
    }
    const explicit = new Set();
    for (const rule of rules) {
      if (!actions.has(rule.action)) failures.push(`invalid transition action: ${language}.${owner.type}`);
      if (rule.action !== 'remove' && !rule.target) failures.push(`transition target missing: ${language}.${owner.type}`);
      for (const member of rule.members || []) {
        if (explicit.has(member)) failures.push(`member mapped more than once: ${language}.${owner.type}.${member}`);
        explicit.add(member);
        if (!actualMembers.has(member)) failures.push(`mapped member is absent from source: ${language}.${owner.type}.${member}`);
      }
    }
    const defaultRule = defaults[0];
    for (const member of actualMembers) {
      const explicitMatches = rules.filter(rule => (rule.members || []).includes(member));
      const applied = explicitMatches.length === 1 ? explicitMatches[0] : defaultRule;
      if (explicitMatches.length > 1 || !applied) {
        failures.push(`actual member does not have one transition decision: ${language}.${owner.type}.${member}`);
      }
    }
  }
}

const listMarkdown = directory => fs.readdirSync(path.join(root, directory), {withFileTypes: true})
  .filter(entry => entry.isFile() && entry.name.endsWith('.md'))
  .map(entry => `${directory}/${entry.name}`);

const commonDocuments = [
  ...listMarkdown('framework/doc/framework/spec'),
  ...listMarkdown('framework/doc/framework/spec/server')
].filter(relative => !relative.endsWith('/90-implementation-gap.ko.md'));

const formalDocuments = [...new Set([...commonDocuments, ...exactDocuments, ...streamConnectorDocuments])];
const routeMeshOwnerNames = new Set([
  'framework/doc/framework/spec/05-framework-api.ko.md',
  'framework/doc/framework/spec/server/10-channel-topology.ko.md',
  'framework/doc/framework/spec/server/11-channel-messaging.ko.md',
  'framework/doc/framework/spec/server/20-spot-messaging.ko.md',
  'framework/doc/framework/spec/server/21-mesh-node.ko.md',
  'framework/doc/framework/spec/server/22-actor-model.ko.md',
  'framework/doc/framework/spec/server/23-spot-actor.ko.md',
  'framework/doc/framework/spec/server/24-spot-address-messaging.ko.md',
  'framework/doc/framework/spec/server/40-location-runtime.ko.md',
  'framework/doc/framework/spec/server/41-location-store-redis.ko.md',
  'framework/doc/framework/spec/server/50-runtime-monitoring.ko.md',
  'framework/doc/framework/spec/server/51-runtime-metrics.ko.md',
  'framework/doc/framework/spec/server/52-message-flow-tracing.ko.md',
  'framework/doc/framework/spec/server/53-flow-correlation.ko.md',
  'framework/doc/framework/spec/server/54-graceful-drain-handoff.ko.md'
]);
const policyDocuments = new Set([...exactDocuments, ...routeMeshOwnerNames]);

const stripCode = text => {
  let inFence = false;
  return text.split(/\r?\n/).map(line => {
    if (/^```/.test(line)) {
      inFence = !inFence;
      return '';
    }
    return inFence ? '' : line;
  }).join('\n');
};

const anchorFor = heading => heading.toLowerCase()
  .replace(/`/g, '')
  .replace(/[^\p{L}\p{N}\s_-]/gu, '')
  .trim()
  .replace(/\s+/g, '-');

const anchorCache = new Map();
const anchorsFor = relative => {
  if (anchorCache.has(relative)) return anchorCache.get(relative);
  const source = read(relative);
  const anchors = new Set();
  if (source !== undefined) {
    for (const line of stripCode(source).split(/\r?\n/)) {
      const heading = /^(#{1,6})\s+(.+?)\s*$/.exec(line);
      if (!heading) continue;
      const anchor = anchorFor(heading[2]);
      if (anchors.has(anchor)) failures.push(`duplicate heading anchor ${anchor}: ${relative}`);
      anchors.add(anchor);
    }
  }
  anchorCache.set(relative, anchors);
  return anchors;
};

for (const relative of formalDocuments) {
  const source = read(relative);
  if (source === undefined) continue;
  if ((source.match(/^```/gm) || []).length % 2 !== 0) failures.push(`unbalanced code fence: ${relative}`);
  if (source.includes('framework/doc/plan/v10.0/')) failures.push(`formal contract references temporary plan: ${relative}`);

  if (policyDocuments.has(relative)) {
    for (const symbol of inventory.forbidden_surface) {
      if (source.includes(symbol)) failures.push(`forbidden 10.0.0 surface ${symbol}: ${relative}`);
    }
    for (const marker of inventory.formal_current_history_markers) {
      if (source.includes(marker)) failures.push(`current/history marker ${marker}: ${relative}`);
    }
  }

  const visible = stripCode(source);
  anchorsFor(relative);

  for (const block of visible.split(/\n(?=[^|])/).filter(part => part.trimStart().startsWith('|'))) {
    const rows = block.split(/\r?\n/).filter(line => line.trim().startsWith('|'));
    if (rows.length < 2) continue;
    const cells = row => row.replace(/\\\|/g, '__ESCAPED_PIPE__').split('|');
    const separator = cells(rows[1]).slice(1, -1).map(cell => cell.trim());
    if (!separator.every(cell => /^:?-{3,}:?$/.test(cell))) continue;
    const width = cells(rows[0]).length;
    for (const row of rows) {
      if (cells(row).length !== width) failures.push(`inconsistent Markdown table width: ${relative}: ${row.trim()}`);
    }
  }

  const linkPattern = /\[[^\]]*\]\(([^)]+)\)/g;
  let link;
  while ((link = linkPattern.exec(visible)) !== null) {
    const target = link[1].trim();
    if (!target || /^[a-z]+:/i.test(target)) continue;
    const [filePart, fragment] = target.split('#', 2);
    let targetRelative = relative;
    if (filePart) {
      const absolute = path.resolve(path.dirname(path.join(root, relative)), decodeURIComponent(filePart));
      if (!fs.existsSync(absolute)) {
        failures.push(`broken relative link ${target}: ${relative}`);
        continue;
      }
      if (fs.statSync(absolute).isDirectory()) continue;
      targetRelative = path.relative(root, absolute).replace(/\\/g, '/');
    }
    if (fragment && !anchorsFor(targetRelative).has(decodeURIComponent(fragment).toLowerCase())) {
      failures.push(`broken heading fragment ${target}: ${relative}`);
    }
  }
}

const featureMapSources = {
  RegistryMessaging: 'framework/doc/framework/common/e2e/config-1-location-messaging.ko.md',
  LocationMessaging: 'framework/doc/framework/common/e2e/config-1-location-messaging.ko.md',
  SpotService: 'framework/doc/framework/common/e2e/config-2-spot-service.ko.md',
  PubSub: 'framework/doc/framework/common/e2e/config-3-pubsub.ko.md',
  RegistrationCodec: 'framework/doc/framework/common/e2e/config-4-registration-codec.ko.md',
  ResilienceLifecycle: 'framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md',
  DiscoveryRegistryHa: 'framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md',
  StoreFailure: 'framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md',
  RuntimeMonitoring: 'framework/doc/framework/common/e2e/config-7-monitoring.ko.md',
  AutomaticTurnDispatch: 'framework/doc/framework/common/e2e/config-8-execution-turn.ko.md',
  ToActorMessaging: 'framework/doc/framework/common/e2e/config-9-to-actor-messaging.ko.md',
  SpotActorTransfer: 'framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md',
  ObservabilityOps: 'framework/doc/framework/common/e2e/config-11-observability-ops.ko.md'
};

const walk = directory => fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
  const absolute = path.join(directory, entry.name);
  if (entry.isDirectory()) {
    if (['build', 'node_modules', '.gradle', 'target'].includes(entry.name)) return [];
    return walk(absolute);
  }
  return [absolute];
});

const featureMaps = walk(path.join(root, 'framework/languages'))
  .filter(absolute => absolute.endsWith('/feature-map.ko.md')
    && /\/e2e(?:-kotlin)?\/[A-Za-z0-9]+\/feature-map\.ko\.md$/.test(absolute));
if (featureMaps.length !== 55) failures.push(`feature-map inventory differs: expected=55 actual=${featureMaps.length}`);

const canonicalIds = new Map();
for (const relative of new Set(Object.values(featureMapSources))) {
  const source = read(relative);
  if (source === undefined) continue;
  const ids = [...source.matchAll(/^#{3,5}\s+([A-Z]{2,4}-[A-Z][0-9]+)\b/gm)].map(match => match[1]);
  if (ids.length === 0 || new Set(ids).size !== ids.length) {
    failures.push(`canonical E2E scenario IDs are absent or duplicated: ${relative}`);
  }
  canonicalIds.set(relative, ids);
}

let featureMapScenarioRows = 0;
for (const absolute of featureMaps) {
  const relative = path.relative(root, absolute).replace(/\\/g, '/');
  const suite = path.basename(path.dirname(absolute));
  const canonicalRelative = featureMapSources[suite];
  if (!canonicalRelative) {
    failures.push(`feature-map suite has no canonical E2E source: ${relative}`);
    continue;
  }
  const source = fs.readFileSync(absolute, 'utf8');
  const leadingIds = [];
  for (const line of source.split(/\r?\n/)) {
    const match = /^(?:[-*]\s+|\|\s*)`?([A-Z]{2,4}-[A-Z][0-9]+)`?\b/.exec(line);
    if (match) leadingIds.push(match[1]);
  }
  const counts = new Map();
  for (const id of leadingIds) counts.set(id, (counts.get(id) || 0) + 1);
  const expectedIds = canonicalIds.get(canonicalRelative) || [];
  featureMapScenarioRows += expectedIds.length;
  for (const id of expectedIds) {
    if ((counts.get(id) || 0) < 1) {
      failures.push(`feature-map requires a dedicated scenario row: ${relative}: ${id}`);
    }
  }
}

// Preserve the exact .NET declaration and C# fixture gate while the unified
// inventory adds the other language projections and transition decisions.
const dotnetDocs = new Map();
for (const relative of dotnetInventory.documents) {
  const source = read(relative);
  if (source !== undefined) dotnetDocs.set(relative, source);
}
const dotnetCombined = [...dotnetDocs.values()].join('\n');
const declaredCounts = new Map();
for (const pattern of [
  /public\s+(?:(?:sealed|abstract|readonly)\s+)*(?:class|interface|enum|record(?:\s+struct)?)\s+([A-Za-z_][A-Za-z0-9_]*)/g,
  /public\s+delegate\s+[A-Za-z_][A-Za-z0-9_<>,.?\[\]\s]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/g
]) {
  let match;
  while ((match = pattern.exec(dotnetCombined)) !== null) {
    declaredCounts.set(match[1], (declaredCounts.get(match[1]) || 0) + 1);
  }
}
const declared = new Set(declaredCounts.keys());
const expected = new Set(dotnetInventory.dotnet_public_symbols);
for (const symbol of expected) if (!declared.has(symbol)) failures.push(`.NET inventory symbol is not declared: ${symbol}`);
for (const symbol of declared) if (!expected.has(symbol)) failures.push(`.NET public declaration missing from inventory: ${symbol}`);
for (const [symbol, count] of declaredCounts) {
  const expectedCount = dotnetInventory.allowed_declaration_counts[symbol] || 1;
  if (count !== expectedCount) failures.push(`.NET declaration count differs: ${symbol} expected=${expectedCount} actual=${count}`);
}
for (const [relative, fixture] of Object.entries(dotnetInventory.csharp_fixture_sha256)) {
  const source = dotnetDocs.get(relative);
  if (source === undefined) continue;
  const blocks = [...source.matchAll(/```csharp\n([\s\S]*?)```/g)]
    .map(match => match[1].replace(/[ \t]+$/gm, '').trim());
  const actual = digest(blocks.join('\n---BLOCK---\n'));
  if (blocks.length !== fixture.block_count || actual !== fixture.sha256) {
    failures.push(`C# exact fixture differs: ${relative}`);
  }
}
for (const symbol of dotnetInventory.required_method_symbols) {
  const escaped = symbol.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  if (!new RegExp(`\\b${escaped}\\s*(?:<[^;{()]*>)?\\s*\\(`).test(dotnetCombined)) {
    failures.push(`.NET required method is not declared: ${symbol}`);
  }
}

if (failures.length > 0) {
  for (const failure of failures) process.stderr.write(`FAIL: ${failure}\n`);
  process.exit(1);
}

process.stdout.write(
  `FRAMEWORK DOC CONTRACTS CLEAN languages=${languages.length} exact_documents=${exactDocuments.size} connector_exact=${connectorLanguages.length} formal_documents=${formalDocuments.length} code_fixtures=${codeFixtureCount} declarations=${declarationCount} transition_owners=${languages.reduce((n, language) => n + inventory.transition_owners[language].length, 0)} transition_members=${transitionMemberCount} feature_maps=${featureMaps.length} scenario_rows=${featureMapScenarioRows}\n`);
NODE
