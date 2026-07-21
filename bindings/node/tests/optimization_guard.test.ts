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

function readNativeRegistrationSources() {
  return [
    path.join(NATIVE_SRC, 'addon.cc'),
    path.join(NATIVE_SRC, 'addon_exports.cc')
  ].map((file) => fs.readFileSync(file, 'utf8')).join('\n');
}

function readTypedNativeBindingMethods() {
  const files = [
    'binding_core.ts',
    'binding_context.ts',
    'binding_socket.ts',
    'binding_eventing.ts',
    'binding_service.ts'
  ];
  const methods = new Set();

  for (const file of files) {
    const body = fs.readFileSync(
      path.join(TS_SRC, 'zlink', 'runtime', 'native', file),
      'utf8'
    );
    for (const match of body.matchAll(/^\s{2}([A-Za-z][A-Za-z0-9_]*)\??\s*:/gm)) {
      methods.add(match[1]);
    }
  }

  return [...methods].sort();
}

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

function moduleSpecifiers(body) {
  return [
    ...body.matchAll(/\bfrom\s+['"]([^'"]+)['"]/g),
    ...body.matchAll(/\brequire\(\s*['"]([^'"]+)['"]\s*\)/g),
    ...body.matchAll(/\bimport\(\s*['"]([^'"]+)['"]\s*\)/g)
  ].map((match) => match[1]);
}

function isRuntimeDeepImport(specifier) {
  return specifier.startsWith('@zlink-systems/zlink/')
    || /(^|\/)(src|dist)\/zlink\/runtime(\/|$)/.test(specifier)
    || /(^|\/)zlink\/runtime(\/|$)/.test(specifier);
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
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_spot_recv_worker.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(
    body,
    /while \(drained < burstCap && currentEpochNs\(\) < fallbackDeadlineNs\)/
  );
});

test('node multi non-routed single-part sends use canonical send builder', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_runtime.ts');
  const body = fs.readFileSync(file, 'utf8');
  const trySend = body.match(
    /function trySocketSend\(socket, \.\.\.args\) \{(?<body>[\s\S]*?)\n\}\n\n\/\/ PERF_MULTI_TEST_POLICY/
  );

  assert.ok(trySend?.groups?.body, 'missing trySocketSend helper');
  assert.doesNotMatch(trySend.groups.body, /sendFrom/);
  assert.match(trySend.groups.body, /\.send\(\)/);
  assert.match(trySend.groups.body, /\.flags\(zlink\.SendFlags\.DontWait\)\.submit\(\)/);
});

test('node multi dealer-dealer receiver uses caller-provided Received storage', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_dealer_dealer_server.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /new zlink\.Received\(\)/);
  assert.match(body, /\.recv\(received, zlink\.RecvFlags\.DontWait\)/);
  assert.doesNotMatch(body, /\brecvNoWaitInto\b/);
  assert.doesNotMatch(body, /\brecvNoWait\b/);
});

test('node Received raw materialization does not create unused hasMore field', () => {
  const nativeBody = fs.readFileSync(path.join(NATIVE_SRC, 'addon_core.cc'), 'utf8');
  const materializerBody = fs.readFileSync(
    path.join(TS_SRC, 'zlink', 'runtime', 'messaging', 'message_materializer.ts'),
    'utf8'
  );

  assert.doesNotMatch(nativeBody, /"hasMore"/);
  assert.doesNotMatch(materializerBody, /\bhasMore\b/);
  assert.match(nativeBody, /if \(routing_id\.size > 0\)[\s\S]*napi_set_named_property \(env, obj, "routingId", rid\)/);
});

test('node SUB single-part materialization keeps the allocation-light raw shape', () => {
  const nativeBody = fs.readFileSync(path.join(NATIVE_SRC, 'addon_core.cc'), 'utf8');
  const materializerBody = fs.readFileSync(
    path.join(TS_SRC, 'zlink', 'runtime', 'messaging', 'message_materializer.ts'),
    'utf8'
  );
  const snapshotBody = fs.readFileSync(
    path.join(TS_SRC, 'zlink', 'runtime', 'messaging', 'message_snapshot.ts'),
    'utf8'
  );

  assert.match(nativeBody, /Hot path: SUB receive usually carries one payload part/);
  assert.match(nativeBody, /if \(part_count == 1\)[\s\S]*napi_set_named_property \(env, obj, "data", data\)/);
  assert.match(nativeBody, /Hot path: plain SUB messages normally have no source routing id/);
  assert.match(nativeBody, /if \(routing_id\.size > 0\)[\s\S]*napi_set_named_property \(env, obj, "routingId", rid\)/);
  assert.match(materializerBody, /Hot path: core SUB messages are normally single-part/);
  assert.match(materializerBody, /messageFromOwnedBuffer\(raw\.data\)/);
  assert.match(snapshotBody, /export function messageFromOwnedBuffer/);
});

test('node SPOT subscribe single-part materialization keeps the allocation-light raw shape', () => {
  const nativeBody = fs.readFileSync(path.join(NATIVE_SRC, 'addon_spot.cc'), 'utf8');
  const materializerBody = fs.readFileSync(
    path.join(TS_SRC, 'zlink', 'runtime', 'messaging', 'message_materializer.ts'),
    'utf8'
  );

  assert.match(nativeBody, /Hot path: SPOT subscribe receive normally carries one payload part/);
  assert.match(nativeBody, /if \(part_count == 1\)[\s\S]*napi_set_named_property \(env, obj, "data", data\)/);
  assert.match(nativeBody, /Hot path: unrouted SPOT subscribe traffic has no source routing id/);
  assert.match(nativeBody, /if \(routing_id\.size > 0\)[\s\S]*napi_set_named_property \(env, obj, "routingId", rid\)/);
  assert.match(materializerBody, /messageFromOwnedBuffer\(raw\.data\)/);
});

test('node native callback buffers become Message facades without snapshot allocation', () => {
  const conversionBody = fs.readFileSync(
    path.join(TS_SRC, 'zlink', 'runtime', 'buffers', 'message_conversion.ts'),
    'utf8'
  );

  assert.match(conversionBody, /HOT PATH: native callbacks already transfer payload ownership/);
  assert.match(conversionBody, /messageFromOwnedBuffer\(buffer \?\? Buffer\.alloc\(0\)\)/);
  assert.doesNotMatch(conversionBody, /return messageFromSnapshot\(\{ data: buffer/);
});

test('node samples and perf stay on public binding contract', () => {
  const files = [
    ...sourceFiles(path.join(ROOT, 'samples'), ['.ts']),
    ...sourceFiles(path.join(ROOT, '..', 'javascript', 'samples'), ['.js']),
    ...sourceFiles(path.join(ROOT, 'perf'), ['.ts'])
  ];
  const forbidden = [
    'requireNative(',
    'nativeHandle(',
    'asRuntimeContext(',
    'asRuntimeSocket(',
    'asRuntimeSpot(',
    'asPublicContract(',
    'NativeHandle',
    'publishDirect',
    'socketTrySubscribePayload',
    'socketSendNoWaitResult',
    'socketSendRoutingNoWaitResult',
    'spotSendToSpotNoWaitResult'
  ];
  const violations = [];

  for (const file of files) {
    const body = fs.readFileSync(file, 'utf8');
    for (const token of forbidden) {
      if (body.includes(token)) {
        violations.push(`${path.relative(ROOT, file)}:${token}`);
      }
    }
    for (const specifier of moduleSpecifiers(body)) {
      if (isRuntimeDeepImport(specifier)) {
        violations.push(`${path.relative(ROOT, file)}:${specifier}`);
      }
    }
  }

  assert.deepEqual(violations, []);
});

test('runtime handles do not expose public native handle bridge methods', () => {
  const files = sourceFiles(path.join(TS_SRC, 'zlink', 'runtime'), ['.ts']);
  const violations = [];

  for (const file of files) {
    const body = fs.readFileSync(file, 'utf8');
    if (/\bnativeHandle\(\)\s*[:{]/.test(body)) {
      violations.push(path.relative(ROOT, file));
    }
    if (/\bgetOptionRaw(?:Strict)?Internal\(/.test(body)) {
      violations.push(path.relative(ROOT, file));
    }
    if (/\bsetOptionRawInternal\(/.test(body)) {
      violations.push(path.relative(ROOT, file));
    }
  }

  assert.deepEqual(violations, []);
});

test('node multi pubsub client uses public TopicMessage subscribe path', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_pubsub_client.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /new zlink\.TopicMessage\(\)/);
  assert.match(body, /\.subscribe\(received, zlink\.RecvFlags\.DontWait\)/);
  assert.match(body, /recordPayload/);
  assert.match(body, /const TOPIC = 'bench'/);
  assert.doesNotMatch(body, /requireNative\(\)/);
  assert.doesNotMatch(body, /nativeHandle\(\)/);
  assert.doesNotMatch(body, /\bsubscribeNoWait\b/);
});

test('node multi publish helper stays on public publish operation', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_runtime.ts');
  const body = fs.readFileSync(file, 'utf8');
  const helper = body.match(
    /function trySocketPublish\(socket, topic, payload\) \{(?<body>[\s\S]*?)\n\}\n\nfunction sleepMs/
  );

  assert.ok(helper?.groups?.body, 'missing trySocketPublish helper');
  assert.match(helper.groups.body, /socket\.publish\(topic\)/);
  assert.doesNotMatch(helper.groups.body, /publishDirect/);
  assert.doesNotMatch(helper.groups.body, /requireNative\(\)/);
});

test('node multi spot reqrep retries public transient submit results', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_spot_reqrep_client.ts');
  const body = fs.readFileSync(file, 'utf8');
  const helper = body.match(
    /function tryRequestSpotReply\(spot, payload, timeoutMs, onReply, onDone\) \{(?<body>[\s\S]*?)\n\}\n\nfunction receiveControlStart/
  );

  assert.ok(helper?.groups?.body, 'missing tryRequestSpotReply helper');
  assert.match(helper.groups.body, /zlink\.SubmitResult\.Backpressured/);
  assert.match(helper.groups.body, /zlink\.SubmitResult\.NotConnected/);
  assert.match(helper.groups.body, /large public SPOT_REQREP runs can observe transient/);
  assert.doesNotMatch(helper.groups.body, /requireNative\(\)/);
  assert.doesNotMatch(helper.groups.body, /nativeHandle\(\)/);
});

test('node multi router-router client uses public routed send path', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_router_router_client.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /trySocketSend\(routers\[i\], SERVER_ROUTING_ID, payloads\[i\]\)/);
  assert.doesNotMatch(body, /requireNative\(\)/);
  assert.doesNotMatch(body, /socketSendRoutingBorrowedNoWaitResult/);
});

test('node binding does not expose borrowed buffer send helpers', () => {
  const files = [
    path.join(ROOT, 'native', 'src', 'addon.cc'),
    path.join(ROOT, 'native', 'src', 'addon_exports.cc'),
    path.join(ROOT, 'native', 'src', 'addon_core.cc'),
    path.join(ROOT, 'native', 'src', 'addon_core_api.h')
  ];
  const body = files.map((file) => fs.readFileSync(file, 'utf8')).join('\n');

  assert.doesNotMatch(body, /socketSendBorrowedNoWaitResult/);
  assert.doesNotMatch(body, /socketSendRoutingBorrowedNoWaitResult/);
  assert.doesNotMatch(body, /init_msg_borrowed_from_bytes/);
});

test('native stream and MeshNode ready callbacks never block Core I/O threads on JS', () => {
  const streamBody = fs.readFileSync(path.join(NATIVE_SRC, 'addon_core.cc'), 'utf8');
  const meshBody = fs.readFileSync(path.join(NATIVE_SRC, 'addon_mesh_service.cc'), 'utf8');
  const streamCallback = streamBody.slice(
    streamBody.indexOf('void stream_on_packet_slot'),
    streamBody.indexOf('typedef void (*stream_slot_packet_callback_t')
  );
  const readyCallback = meshBody.slice(
    meshBody.indexOf('zlink_mesh_ready_domain_mask_t ready_handler_bridge'),
    meshBody.indexOf('void ready_tsfn_finalize')
  );

  assert.match(streamCallback, /napi_tsfn_nonblocking/);
  assert.doesNotMatch(streamCallback, /napi_tsfn_blocking/);
  assert.match(readyCallback, /napi_tsfn_nonblocking/);
  assert.doesNotMatch(readyCallback, /napi_tsfn_blocking|condition_variable|\.wait\s*\(/);
});

test('native addon registration stays limited to runtime-owned methods', () => {
  const body = readNativeRegistrationSources();
  const removedExports = [
    'socketPerfDealerDealerSendLoop',
    'socketPerfDealerRouterEchoLoop',
    'socketSendFrom',
    'socketRecvHandler',
    'socketSubscribePayloadInto',
    'socketTrySubscribePayload',
    'socketSubscribeHandler',
    'socketRecvInto',
    'socketRecvMsgInto',
    'socketStreamAttachPacketEcho',
    'socketRouteEchoStep',
    'socketStreamDetach',
    'socketStreamPeerRoutingId',
    'socketStreamSend',
    'routerHandlerMessage',
    'routerRecvInto',
    'routerRecvPayloadInto',
    'pollerWaitMany',
    'registryStart',
    'registrySetSockOpt',
    'discoveryProviderCount',
    'discoveryServiceAvailable',
    'providerNew',
    'providerBind',
    'providerConnectRegistry',
    'providerRegister',
    'providerUpdateWeight',
    'providerUnregister',
    'providerRegisterResult',
    'providerSetTlsServer',
    'providerRouter',
    'providerRouterPeers',
    'providerSetSockOpt',
    'providerDestroy',
    'spotNodeRegister',
    'spotNodeUnregister',
    'spotNodePubSocket',
    'spotNodeSubSocket',
    'spotNodePubPeers',
    'spotNodeSubPeers',
    'spotTryPublish',
    'spotSendToSpotFrom',
    'spotRequestSpotFrom',
    'spotAttachRouteEcho',
    'spotPerfSendSendLoop',
    'spotRecvRoutedPayloadInto',
    'spotSubscribePattern',
    'spotRecvPayloadInto',
    'spotSubscribeHandler'
  ];

  for (const name of removedExports) {
    assert.doesNotMatch(body, new RegExp(`ZLINK_METHOD\\s*\\("${name}"`));
  }
});

test('native addon registration matches the typed native binding surface', () => {
  const addon = readNativeRegistrationSources();
  const registered = [...addon.matchAll(/ZLINK_METHOD\s*\("([^"]+)"/g)]
    .map((match) => match[1])
    .sort();
  const typed = readTypedNativeBindingMethods();

  assert.deepEqual(registered, typed);
});

test('node multi orchestrator ignores closed child stdin pipes', () => {
  const file = path.join(ROOT, 'perf', 'multi', 'perf_multi_orchestrator.ts');
  const body = fs.readFileSync(file, 'utf8');

  assert.match(body, /function writeChildLine/);
  assert.match(body, /isClosedPipeError/);
  assert.match(body, /writeChildLine\(server, 'STOP\\n', \{ end: true \}\)/);
});
