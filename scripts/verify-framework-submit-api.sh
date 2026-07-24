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
      '반환 데이터 없이 완료',
      '`DeadlineExceeded`',
      '`RuntimeShutdown`']],
    [frameworkApi, 'Framework API', [
      '반환 데이터 없이 정상 완료',
      'monitoring metric과 runtime event',
      '`RuntimeShutdown`']],
    [spotMessaging, 'Spot messaging', [
      '정상 완료는 source-local outbound queue가 operation을 수락했다는 뜻',
      'public 반환값이 아니라 monitoring metric과 event']],
    [scenario, 'Config 13', [
      '원격 handler 실행 완료가 아니다',
      'timeout 예외',
      'shutdown']]
  ]) {
    for (const fragment of fragments) {
      if (!source.includes(fragment)) fail(`${owner} is missing submit semantic: ${fragment}`);
    }
  }

  const contracts = new Map(languages.map(language => [
    language, readExactContract(root, language, tags[language]),
  ]));
  const submitPatterns = {
    dotnet: /ValueTask\s+Async\s*\(/,
    cpp: /task_t<void>\s+submit\s*\(\s*\)/,
    java: /CompletionStage<Void>\s+submit\s*\(/,
    kotlin: /suspend\s+fun\s+await\s*\(\s*\)(?:\s*:\s*Unit)?/,
    node: /submit\([^)]*\): Promise<void>/,
  };
  for (const language of languages) {
    const contract = contracts.get(language);
    if (!submitPatterns[language].test(contract.source)) {
      fail(`${language} async-only submit projection is missing`);
    }
    if (/\b(?:TrySubmit|trySubmit|try_submit)\s*(?:<[^>]*>)?\s*\(/.test(contract.code)) {
      fail(`${language} exact public declarations expose a synchronous TrySubmit terminator`);
    }
    if (/\b(?:ZLinkSubmitStatus|ZLinkSubmitResult|ZLinkPublishResult|ZLinkLogicalMulticastDetail|submit_status_t|submit_result_t|publish_result_t|logical_multicast_detail_t)\b/.test(contract.code)) {
      fail(`${language} exact public declarations expose a removed one-way result type`);
    }
    if (language === 'dotnet' && /\bSubmitAsync\s*\(/.test(contract.code)) {
      fail('dotnet exact public declarations retain SubmitAsync');
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
    dotnet: 'unreachable count',
    cpp: 'remote_unreachable_count',
    java: 'unreachable',
    node: 'remoteUnreachableCount',
  };
  for (const [language, fragment] of Object.entries(multicastFragments)) {
    if (!contracts.get(language).source.includes(fragment)) {
      fail(`${language} Logical Multicast monitoring omits unreachable remote targets`);
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
      /fun publish\(\s*channelName: String,\s*event: Any/s,
      /fun publish\(\s*channelName: String,\s*topic: String,\s*event: Any/s,
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

  for (const fragment of [
    '즉시 수락',
    'capacity 대기',
    'timeout 예외',
    'cancellation',
    'shutdown',
    'target',
    'route',
    'target이 0개',
    'partial',
    'monitoring',
  ]) {
    if (!scenario.toLowerCase().includes(fragment.toLowerCase())) {
      fail(`Config 13 is missing one-way scenario coverage: ${fragment}`);
    }
  }

  if (failures.length) {
    process.stderr.write(`${failures.map(message => `[submit-api] FAIL: ${message}`).join('\n')}\n`);
    process.exit(1);
  }
  process.stdout.write(`[submit-api] contract CLEAN languages=${languages.length} result_free=1 fanout_overloads=10\n`);
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
    if (/\b(?:ZLinkSubmitStatus|ZLinkSubmitResult|ZLinkPublishResult|ZLinkLogicalMulticastDetail|submit_status_t|publish_result_t|logical_multicast_detail_t)\b/.test(source)) {
      fail(`${language} public source retains a removed one-way result type: ${path.relative(root, file)}`);
    }
    if (language === 'dotnet' && /\bSubmitAsync\s*\(/.test(source)) {
      fail(`dotnet public source retains SubmitAsync: ${path.relative(root, file)}`);
    }
  }
}
if (failures.length) {
  process.stderr.write(`${failures.map(message => `[submit-api] IMPLEMENTATION GAP: ${message}`).join('\n')}\n`);
  process.exit(1);
}
process.stdout.write(`[submit-api] implementation CLEAN public_files=${scanned}\n`);
NODE
