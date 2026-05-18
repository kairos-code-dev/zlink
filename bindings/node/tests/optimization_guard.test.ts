'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.basename(path.resolve(__dirname, '..')) === 'dist-tools'
  ? path.resolve(__dirname, '..', '..')
  : path.resolve(__dirname, '..');
const NATIVE_SRC = path.join(ROOT, 'native', 'src');
const TS_SRC = path.join(ROOT, 'src');

const aggregateSymbols = [
  'zlink_send',
  'zlink_recv',
  'zlink_publish',
  'zlink_subscribe',
  'zlink_router_recv',
  'zlink_dealer_request',
  'zlink_router_request',
  'zlink_router_reply',
  'zlink_spot_send_channel',
  'zlink_spot_request_channel',
  'zlink_spot_request_spot',
  'zlink_spot_request_router',
  'zlink_spot_publish',
  'zlink_spot_subscribe',
  'zlink_spot_send_spot',
  'zlink_spot_reply_spot',
  'zlink_spot_reply_router',
  'zlink_spot_recv'
];

const requiredPartSymbols = [
  'zlink_send_part',
  'zlink_recv_part',
  'zlink_publish_part',
  'zlink_subscribe_part',
  'zlink_router_recv_part',
  'zlink_dealer_request_part',
  'zlink_router_request_part',
  'zlink_router_reply_part',
  'zlink_spot_publish_part',
  'zlink_spot_subscribe_part',
  'zlink_spot_request_channel_part',
  'zlink_spot_request_spot_part',
  'zlink_spot_reply_router_part'
];

function sourceFiles(dir, suffixes) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      out.push(...sourceFiles(full, suffixes));
    } else if (suffixes.some((suffix) => entry.name.endsWith(suffix))) {
      out.push(full);
    }
  }
  return out;
}

test('native hot paths use part substrate instead of aggregate calls', () => {
  const files = sourceFiles(NATIVE_SRC, ['.cc', '.h']);
  const text = files.map((file) => fs.readFileSync(file, 'utf8')).join('\n');

  for (const symbol of requiredPartSymbols) {
    assert.ok(text.includes(symbol), `missing required helper substrate ${symbol}`);
  }

  const violations = [];
  for (const file of files) {
    const body = fs.readFileSync(file, 'utf8');
    for (const symbol of aggregateSymbols) {
      const pattern = new RegExp(`\\b${symbol}\\s*\\(`, 'g');
      for (const match of body.matchAll(pattern)) {
        if (body.slice(match.index).startsWith(`${symbol}_part`)) continue;
        violations.push(`${path.relative(ROOT, file)}:${symbol}`);
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('TypeScript facade does not depend on dynamic ffi packages', () => {
  const files = sourceFiles(TS_SRC, ['.ts']);
  const text = files.map((file) => fs.readFileSync(file, 'utf8')).join('\n');

  assert.equal(text.includes('ffi-napi'), false);
  assert.equal(text.includes('ref-napi'), false);
  assert.equal(text.includes('node-ffi'), false);
});

test('node multi spot drain checks fallback deadline inside each burst', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_spot_client.ts');
  const body = fs.readFileSync(file, 'utf8');
  const innerBurst = body.match(
    /while \(drained < burstCap\) \{(?<body>[\s\S]*?)if \(!trySpotSubscribeInto/
  );

  assert.ok(innerBurst?.groups?.body, 'missing spot inner burst drain loop');
  assert.match(innerBurst.groups.body, /currentEpochNs\(\) >= fallbackDeadlineNs/);
});

test('node multi non-routed single-part sends use public sendFrom fast path', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_runtime.ts');
  const body = fs.readFileSync(file, 'utf8');
  const trySend = body.match(
    /function trySocketSend\(socket, \.\.\.args\) \{(?<body>[\s\S]*?)\n\}\n\n\/\/ PERF_MULTI_TEST_POLICY/
  );

  assert.ok(trySend?.groups?.body, 'missing trySocketSend helper');
  assert.match(trySend.groups.body, /typeof socket\.sendFrom === 'function'/);
  assert.match(trySend.groups.body, /socket\.sendFrom\(payload, zlink\.SendFlags\.DontWait\)/);
});

test('node multi dealer-dealer receiver uses raw public recvInto path', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_dealer_dealer_server.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /\.recvInto\(/);
  assert.doesNotMatch(body, /\brecvNoWaitInto\b/);
  assert.doesNotMatch(body, /\brecvNoWait\b/);
});

test('node multi pubsub client reuses caller-provided topic storage', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_pubsub_client.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /subscribeNoWaitInto/);
  assert.doesNotMatch(body, /\bsubscribeNoWait\b/);
});

test('node multi orchestrator ignores closed child stdin pipes', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_orchestrator.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /function writeChildLine/);
  assert.match(body, /isClosedPipeError/);
  assert.match(body, /writeChildLine\(server, 'STOP\\n', \{ end: true \}\)/);
});
