#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:---contract}"
case "$mode" in
  --contract|--implementation) ;;
  *) echo "usage: $0 [--contract|--implementation]" >&2; exit 2 ;;
esac

node - "$repo_root" "$mode" <<'NODE'
'use strict';

const fs = require('fs');
const path = require('path');
const root = process.argv[2];
const mode = process.argv[3];
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
    fail(`missing contract file: ${relative}`);
    return '';
  }
  return fs.readFileSync(absolute, 'utf8');
}

if (mode === '--contract') {
  const asyncPolicy = read('framework/doc/framework/spec/04-async-execution-policy.ko.md');
  const frameworkApi = read('framework/doc/framework/spec/05-framework-api.ko.md');
  const spotMessaging = read('framework/doc/framework/spec/server/20-spot-messaging.ko.md');
  const scenario = read('framework/doc/framework/common/e2e/config-13-submit-admission.ko.md');
  for (const [source, owner, fragments] of [
    [asyncPolicy, 'async policy', [
      '동기 `TrySubmit` 계열을 제공하지 않는다',
      '비동기 submit terminator 하나만 제공하고',
      'remote handler 완료는 기다리지 않는다']],
    [frameworkApi, 'Framework API', [
      '`Submitted`', '`Backpressured`', '`TimedOut`',
      'outbound admission', 'exceptional completion']],
    [spotMessaging, 'Spot messaging', [
      'local outbound admission만 나타내고 target Spot handler 실행은 기다리지 않는다']],
    [scenario, 'Config 13', [
      'Submit 완료는 원격 handler 실행 완료가 아니다',
      'Timeout을 늘리거나 반복 submit해서 성공시키는 절차는 허용하지 않는다']]
  ]) {
    for (const fragment of fragments) {
      if (!source.includes(fragment)) fail(`${owner} is missing submit semantic: ${fragment}`);
    }
  }

  const contracts = new Map(languages.map(language => [
    language, readExactContract(root, language, tags[language]),
  ]));
  const submitPatterns = {
    dotnet: /ValueTask<ZLinkSubmitResult>\s+SubmitAsync\s*\(/,
    cpp: /task_t<submit_result_t>\s+submit\s*\(\s*\)/,
    java: /CompletionStage<[^>\n]*ZLinkSubmitResult>\s+submit\s*\(/,
    kotlin: /CompletionStage<T>\.await\(\): T/,
    node: /submit\([^)]*\): Promise<ZLinkSubmitResult>/,
  };
  for (const language of languages) {
    const contract = contracts.get(language);
    if (!submitPatterns[language].test(contract.source)) {
      fail(`${language} async-only submit projection is missing`);
    }
    if (/\b(?:TrySubmit|trySubmit|try_submit)\s*(?:<[^>]*>)?\s*\(/.test(contract.code)) {
      fail(`${language} exact public declarations expose a synchronous TrySubmit terminator`);
    }
    if (/\b(?:ActorRelocationTimeout|actorRelocationTimeout|actor_relocation_timeout)\b/.test(contract.code)) {
      fail(`${language} exact public declarations expose an Actor-specific relocation timeout`);
    }
  }

  const actorForwardWindowFragments = {
    dotnet: 'RelocationForwardingWindow',
    cpp: 'relocation_forwarding_window',
    java: 'relocationForwardingWindow',
    node: 'relocationForwardingWindowMs',
  };
  for (const [language, fragment] of Object.entries(actorForwardWindowFragments)) {
    if (!contracts.get(language).source.includes(fragment)) {
      fail(`${language} host-wide Actor forwarding window is missing`);
    }
  }

  const directRouteContextFragments = {
    dotnet: ['ZLinkRouteSendContext', 'string MeshName', 'RoutingId SourceNodeRid'],
    cpp: ['route_handler_context_t', 'std::string mesh_name', 'source_node_rid'],
    java: ['ZLinkRouteSendContext', 'meshName()', 'sourceNodeRid()'],
    node: ['ZLinkRouteSendContext', 'readonly meshName: string', 'readonly sourceNodeRid: RoutingId'],
  };
  for (const [language, fragments] of Object.entries(directRouteContextFragments)) {
    const source = contracts.get(language).source;
    for (const fragment of fragments) {
      if (!source.includes(fragment)) {
        fail(`${language} direct-node handler context is missing ${fragment}`);
      }
    }
  }

  const multicastFragments = {
    dotnet: 'UnreachableRemoteNodeCount',
    cpp: 'unreachable_remote_node_count',
    java: 'unreachableRemoteNodeCount',
    node: 'unreachableRemoteNodeCount',
  };
  for (const [language, fragment] of Object.entries(multicastFragments)) {
    if (!contracts.get(language).source.includes(fragment)) {
      fail(`${language} Logical Multicast result omits unreachable remote targets`);
    }
  }

  const dotnetMulticast = contracts.get('dotnet').code;
  if (!/IZLinkPublishCall\s+Publish<TEvent>\(\s*string channelName,\s*string topic,\s*TEvent message\)/s.test(dotnetMulticast)) {
    fail('dotnet Logical Multicast ChannelName-and-topic projection is missing');
  }
  if (/\bPublishSpot\s*</.test(dotnetMulticast)
      || /Publish<TEvent>\(\s*string meshName,\s*string channelName/s.test(dotnetMulticast)
      || /IZLinkPublishCall\s+Publish<TEvent>\(\s*string topic,\s*TEvent message\)/s.test(dotnetMulticast)) {
    fail('dotnet Logical Multicast retains an alias, MeshName overload, or implicit ChannelName overload');
  }

  const cppMulticast = contracts.get('cpp').code;
  if (!/publish_call_t\s+publish\(\s*std::string channel_name,\s*std::string topic,/s.test(cppMulticast)
      || !/add_subscribe\(\s*std::string channel_name,\s*std::string topic\)/s.test(cppMulticast)) {
    fail('cpp Logical Multicast and subscription must both name ChannelName and topic');
  }
  if (/send_call_t\s+publish\(\s*std::string topic,/s.test(cppMulticast)) {
    fail('cpp Spot context retains an implicit ChannelName publish overload');
  }

  // Classic fanout keeps both public meanings: the convenience overload derives
  // the topic from the packet name, while the second overload accepts a topic.
  const fanoutChecks = {
    dotnet: [
      /Publish<TEvent>\(\s*string channelName,\s*TEvent message\)/s,
      /Publish<TEvent>\(\s*string channelName,\s*string topic,\s*TEvent message\)/s,
    ],
    cpp: [
      /publish\(std::string channel_name,\s*(?:const )?TEvent(?:\s*&)?\s*event\)/s,
      /publish\(std::string channel_name,\s*std::string topic,\s*(?:const )?TEvent(?:\s*&)?\s*event\)/s,
    ],
    java: [
      /publish\(\s*String channelName,\s*Object event\)/s,
      /publish\(\s*String channelName,\s*String topic,\s*Object event\)/s,
    ],
    kotlin: [
      /publishToTopic\(\s*channelName: String,\s*message: TEvent/s,
      /publishToTopic\(\s*channelName: String,\s*topic: String,\s*message: TEvent/s,
    ],
    node: [
      /publish\(channelName: string, event: unknown\): ZLinkFanoutPublishCall/,
      /publish\(channelName: string, topic: string, event: unknown\): ZLinkFanoutPublishCall/,
    ],
  };
  for (const [language, patterns] of Object.entries(fanoutChecks)) {
    const source = contracts.get(language).source;
    if (!patterns[0].test(source)) fail(`${language} classic fanout packet-name-derived overload is missing`);
    if (!patterns[1].test(source)) fail(`${language} classic fanout explicit-topic overload is missing`);
  }

  const headings = [...scenario.matchAll(/^###\s+(SA-(?:E2E|REG)-\d{2})\b/gm)]
    .map(match => match[1]);
  const expected = [];
  for (let index = 1; index <= 20; index += 1) {
    expected.push(`SA-E2E-${String(index).padStart(2, '0')}`);
  }
  for (let index = 1; index <= 4; index += 1) {
    expected.push(`SA-REG-${String(index).padStart(2, '0')}`);
  }
  if (JSON.stringify(headings) !== JSON.stringify(expected)) {
    fail(`Config 13 scenario heading schema differs: expected=${expected.length} actual=${headings.length}`);
  }

  if (failures.length) {
    process.stderr.write(`${failures.map(message => `[submit-api] FAIL: ${message}`).join('\n')}\n`);
    process.exit(1);
  }
  process.stdout.write(`[submit-api] contract CLEAN languages=${languages.length} scenarios=20 regressions=4 fanout_overloads=10\n`);
  process.exit(0);
}

function walk(directory, extensions, output = []) {
  if (!fs.existsSync(directory)) return output;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const absolute = path.join(directory, entry.name);
    if (entry.isDirectory()) walk(absolute, extensions, output);
    else if (extensions.some(extension => entry.name.endsWith(extension))) output.push(absolute);
  }
  return output;
}

// Implementation mode is intentionally separate from the document gate. It
// scans only installed/public source roots; internal non-blocking primitives
// may keep their implementation-specific names.
const publicRoots = [
  ['dotnet', 'framework/languages/dotnet/src/Zlink.Framework/Contracts', ['.cs']],
  ['cpp', 'framework/languages/cpp/framework/include', ['.h', '.hpp']],
  ['java', 'framework/languages/java/zlink-framework-core/src/main/java', ['.java']],
  ['kotlin', 'framework/languages/java/zlink-framework-kotlin/src/main/kotlin', ['.kt']],
  ['node', 'framework/languages/node/packages/framework/src/contracts', ['.ts']],
];
let scanned = 0;
for (const [language, relative, extensions] of publicRoots) {
  const absolute = path.join(root, relative);
  if (!fs.existsSync(absolute)) {
    fail(`${language} public source root is missing: ${relative}`);
    continue;
  }
  for (const file of walk(absolute, extensions)) {
    scanned += 1;
    const source = fs.readFileSync(file, 'utf8');
    const exposesRemovedTerminator = language === 'java' || language === 'kotlin'
      ? /^\s*public\s+[^\n;{}]*\b(?:TrySubmit|trySubmit|try_submit)\s*(?:<[^>]*>)?\s*\(/m.test(source)
      : /\b(?:TrySubmit|trySubmit|try_submit)\s*(?:<[^>]*>)?\s*\(/.test(source);
    if (exposesRemovedTerminator) {
      fail(`${language} public source retains TrySubmit: ${path.relative(root, file)}`);
    }
  }
}
if (failures.length) {
  process.stderr.write(`${failures.map(message => `[submit-api] IMPLEMENTATION GAP: ${message}`).join('\n')}\n`);
  process.exit(1);
}
process.stdout.write(`[submit-api] implementation CLEAN public_files=${scanned}\n`);
NODE
