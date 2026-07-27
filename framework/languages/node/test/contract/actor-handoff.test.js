const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const actorHandoff = require('../../packages/framework/dist/runtime/actors/actor-handoff');

function frame(value) {
  return [zlink.Message.from(`header:${value}`), zlink.Message.from(value)];
}

function actorRef(generation = 1n) {
  return {
    nodeRid: zlink.RoutingId.from('source'),
    actorId: 'actor-1',
    objectGeneration: generation,
    meshName: 'mesh'
  };
}

function target(name = 'target') {
  return {
    routerChannelId: 'mesh',
    targetNodeRid: zlink.RoutingId.from(`${name}-node`),
    spotId: zlink.RoutingId.from(`${name}-spot`),
    spotKind: framework.ZLinkSpotKind.User,
    authorityOwnerGeneration: 2n
  };
}

function targetActorRef(name = 'target', generation = 2n) {
  return {
    nodeRid: zlink.RoutingId.from(`${name}-node`),
    actorId: 'actor-1',
    objectGeneration: generation,
    meshName: 'mesh'
  };
}

function harness(messageFollowDurationMs = 30) {
  const followed = [];
  const messageFollowPayloads = [];
  const markers = [];
  let currentGeneration = 2n;
  const transport = {
    async sendToSpot(_target, payload) {
      messageFollowPayloads.push(payload);
      followed.push(Buffer.from(payload.payload, 'base64').toString());
    },
    async requestToSpot() {
      return { ok: true };
    }
  };
  const coordinator = new framework.ZLinkActorHandoffCoordinator({
    routedTransport: transport,
    messageFollowDurationMs,
    isStaleActorRef: (_actorId, ref) => ref.objectGeneration !== currentGeneration,
    isCurrentHandoffTarget: (_actorId, spotId) => spotId === 'target-spot',
    requestSource: () => ({
      ownerId: 'source-owner',
      ownerLeaseGeneration: 1n,
      nodeRid: 'source-node',
      nodeGeneration: 1n
    }),
    onMarker: (marker, actorId, index) => markers.push({ marker, actorId, index })
  });
  return {
    coordinator,
    followed,
    messageFollowPayloads,
    markers,
    setCurrentGeneration(value) { currentGeneration = value; }
  };
}

test('in-flight handoff preserves moving packet arrival order in the commit backlog', async () => {
  const { coordinator, markers } = harness();
  coordinator.begin('actor-1', 1n);
  for (const value of ['P1', 'P2', 'P3']) {
    const parts = frame(value);
    await coordinator.capture('actor-1', parts, false, undefined, actorRef());
    parts.forEach((part) => part.close());
  }

  const backlog = coordinator.snapshot('actor-1');
  assert.deepEqual(backlog.map((packet) => Buffer.from(packet.payload, 'base64').toString()), ['P1', 'P2', 'P3']);
  assert.deepEqual(backlog.map((packet) => packet.index), [0, 1, 2]);
  assert.deepEqual(markers.map((entry) => entry.marker), [
    'handoff_backlog',
    'handoff_backlog',
    'handoff_backlog'
  ]);
});

test('Message Follow preserves operation identity and increments a bounded hop count', async () => {
  const { coordinator, messageFollowPayloads } = harness();
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());
  const ref = Object.assign(actorRef(1n), {
    handoffOperationId: 'source-op-7',
    handoffMessageFollowHopCount: 7
  });
  const parts = frame('last-hop');
  await coordinator.capture('actor-1', parts, false, undefined, ref);
  parts.forEach((part) => part.close());
  assert.equal(messageFollowPayloads[0].operationId, 'source-op-7');
  assert.equal(messageFollowPayloads[0].messageFollowHopCount, 8);

  const loop = frame('loop');
  const exhausted = Object.assign(actorRef(1n), {
    handoffOperationId: 'source-op-8',
    handoffMessageFollowHopCount: 8
  });
  await assert.rejects(
    coordinator.capture('actor-1', loop, false, undefined, exhausted),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );
  loop.forEach((part) => part.close());
});

test('Message Follow request preserves its absolute deadline and drops a late relay reply', async () => {
  const requests = [];
  let completeRelay;
  const coordinator = new framework.ZLinkActorHandoffCoordinator({
    routedTransport: {
      async sendToSpot() {},
      async requestToSpot(_target, payload, options) {
        requests.push({ payload, options });
        return await new Promise((resolve) => { completeRelay = resolve; });
      }
    },
    messageFollowDurationMs: 1_000,
    requestTimeoutMs: 30_000
  });
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const deadlineUnixMs = Date.now() + 80;
  const requestHeader = streamProtocol.encodeStreamHeader({
    kind: streamProtocol.ZLinkStreamMessageKind.Request,
    codec: streamProtocol.ZLinkStreamCodec.Json,
    flags: streamProtocol.ZLinkStreamHeaderFlags.HasRequestSeq
      | streamProtocol.ZLinkStreamHeaderFlags.HasMetadata
      | streamProtocol.ZLinkStreamHeaderFlags.HasCorrelationId,
    requestSeq: 19n,
    name: 'DeadlineRequest',
    metadata: streamProtocol.actorRequestDeadlineMetadata(deadlineUnixMs),
    correlationId: 'deadline-correlation'
  });
  const parts = [
    zlink.Message.from(Buffer.from(requestHeader)),
    zlink.Message.from(Buffer.from(JSON.stringify({ marker: 'deadline' })))
  ];
  const reply = coordinator.capture(
    'actor-1',
    parts,
    true,
    undefined,
    actorRef(1n)
  );
  parts.forEach((part) => part.close());

  await assert.rejects(
    reply,
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.DeadlineExceeded
  );
  assert.equal(requests.length, 1);
  assert.equal(requests[0].payload.deadlineUnixMs, deadlineUnixMs);
  const relayedHeader = streamProtocol.decodeStreamHeader(
    Buffer.from(requests[0].payload.header, 'base64')
  );
  assert.equal(relayedHeader.correlationId, 'deadline-correlation');
  assert.ok(requests[0].options.timeoutMs > 0);
  assert.ok(requests[0].options.timeoutMs <= 80);

  completeRelay({ ok: true, response: 'late' });
  await new Promise((resolve) => setImmediate(resolve));
});

test('Message Follow rejects an expired request before target transport admission', async () => {
  let requests = 0;
  const coordinator = new framework.ZLinkActorHandoffCoordinator({
    routedTransport: {
      async sendToSpot() {},
      async requestToSpot() {
        requests += 1;
        return { ok: true };
      }
    },
    messageFollowDurationMs: 1_000
  });
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const parts = frame('expired');
  await assert.rejects(
    coordinator.capture('actor-1', parts, true, undefined, actorRef(1n), Date.now() - 1),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.DeadlineExceeded
  );
  parts.forEach((part) => part.close());
  assert.equal(requests, 0);
});

test('accepted handoff rejects an expired request before replay queue admission', async () => {
  const { coordinator } = harness();
  coordinator.begin('actor-1', 1n);
  const parts = frame('expired-accepted');
  const pending = coordinator.capture(
    'actor-1',
    parts,
    true,
    undefined,
    actorRef(1n),
    Date.now() - 1
  );
  parts.forEach((part) => part.close());
  const backlog = coordinator.snapshot('actor-1');
  let admitted = 0;
  let dispatched = 0;
  const results = await actorHandoff.replayActorHandoffBacklog(
    backlog,
    async () => { dispatched += 1; },
    () => { admitted += 1; }
  );

  assert.equal(admitted, 0);
  assert.equal(dispatched, 0);
  assert.equal(results[0].errorKind, framework.ZLinkFrameworkErrorKind.DeadlineExceeded);
  coordinator.cancel('actor-1');
  await assert.rejects(pending);
});

test('Message Follow route rejects a queue above 1024 messages without a Store lookup or retry', async () => {
  const coordinator = new framework.ZLinkActorHandoffCoordinator({
    routedTransport: { sendToSpot: async () => new Promise(() => {}) },
    messageFollowDurationMs: 1000
  });
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());
  for (let index = 0; index < 1024; index++) {
    const parts = frame(`queued-${index}`);
    void coordinator.capture('actor-1', parts, false, undefined, actorRef(1n));
    parts.forEach((part) => part.close());
  }
  const overflow = frame('overflow');
  assert.throws(
    () => coordinator.capture('actor-1', overflow, false, undefined, actorRef(1n)),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );
  overflow.forEach((part) => part.close());
});

test('packets captured after the commit snapshot use Message Follow after backlog completion', async () => {
  const { coordinator, followed } = harness();
  coordinator.begin('actor-1', 1n);
  const backlogParts = frame('B1');
  await coordinator.capture('actor-1', backlogParts, false, undefined, actorRef());
  backlogParts.forEach((part) => part.close());
  const backlog = coordinator.snapshot('actor-1');

  const directParts = frame('D1');
  await coordinator.capture('actor-1', directParts, false, undefined, actorRef());
  directParts.forEach((part) => part.close());
  assert.deepEqual(followed, []);

  coordinator.complete(
    'actor-1',
    target(),
    targetActorRef(),
    backlog.map((packet) => ({ index: packet.index, ok: true }))
  );
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(followed, ['D1']);
});

test('bound-session packets keep one sequence across snapshot and Message Follow activation', async () => {
  const { coordinator, followed } = harness();
  const sessionTarget = {
    routerChannelId: 'session-mesh',
    targetNodeRid: zlink.RoutingId.from('session-node'),
    spotId: zlink.RoutingId.from('session-spot')
  };
  coordinator.begin('actor-1', 1n);
  for (const value of ['S1', 'S2']) {
    const parts = frame(value);
    await coordinator.capture('actor-1', parts, false, sessionTarget, actorRef());
    parts.forEach((part) => part.close());
  }
  const backlog = coordinator.snapshot('actor-1');
  const s3 = frame('S3');
  await coordinator.capture('actor-1', s3, false, sessionTarget, actorRef());
  s3.forEach((part) => part.close());
  coordinator.complete(
    'actor-1',
    target(),
    targetActorRef(),
    backlog.map((packet) => ({ index: packet.index, ok: true }))
  );
  const s4 = frame('S4');
  await coordinator.capture('actor-1', s4, false, sessionTarget, actorRef());
  s4.forEach((part) => part.close());
  await new Promise((resolve) => setImmediate(resolve));

  assert.deepEqual(
    backlog.map((packet) => Buffer.from(packet.payload, 'base64').toString()).concat(followed),
    ['S1', 'S2', 'S3', 'S4']
  );
  assert.equal(backlog[0].remoteBoundSessionTarget.routerChannelId, 'session-mesh');
});

test('Message Follow relays before duration expiry and rejects after route removal', async () => {
  const { coordinator, followed, markers } = harness(10);
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const inside = frame('G1');
  await coordinator.capture('actor-1', inside, false, undefined, actorRef(1n));
  inside.forEach((part) => part.close());
  assert.deepEqual(followed, ['G1']);

  await new Promise((resolve) => setTimeout(resolve, 20));
  const outside = frame('G2');
  await assert.rejects(
    coordinator.capture('actor-1', outside, false, undefined, actorRef(1n)),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );
  outside.forEach((part) => part.close());
  assert.equal(markers.some((entry) => entry.marker === 'message_follow_route_removed'), true);
  assert.equal(markers.some((entry) => entry.marker === 'message_follow_expired'), true);
});

test('a Core-routed packet owned by the current actor bypasses an older Message Follow route', async () => {
  const { coordinator, followed, markers } = harness();
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const current = frame('current-owner');
  assert.equal(
    coordinator.capture('actor-1', current, false, undefined, actorRef(2n)),
    undefined
  );
  current.forEach((part) => part.close());

  assert.deepEqual(followed, []);
  assert.equal(markers.some((entry) => entry.marker === 'message_follow_relay'), false);
});

test('a Core-routed packet marked with the current target bypasses stale owner generation', async () => {
  const { coordinator, followed, markers } = harness();
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const owner = Object.assign(actorRef(1n), {
    handoffMessageFollowed: true,
    handoffTargetSpotId: 'target-spot'
  });
  const current = frame('current-target');
  assert.equal(
    coordinator.capture('actor-1', current, false, undefined, owner),
    undefined
  );
  current.forEach((part) => part.close());

  assert.deepEqual(followed, []);
  assert.equal(markers.some((entry) => entry.marker === 'message_follow_relay'), false);
});

test('chained relocation replaces the local Message Follow route and removes it after cutoff', async () => {
  const { coordinator, followed } = harness(10);
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target('first'), targetActorRef('first'));
  assert.equal(coordinator.messageFollowCount('actor-1'), 1);

  coordinator.begin('actor-1', 2n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target('second'), targetActorRef('second', 3n));
  assert.equal(coordinator.messageFollowCount('actor-1'), 1);
  const packet = frame('chain');
  await coordinator.capture('actor-1', packet, false, undefined, actorRef(2n));
  packet.forEach((part) => part.close());
  assert.deepEqual(followed, ['chain']);

  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.equal(coordinator.messageFollowCount('actor-1'), 0);
});

test('in-flight request preserves framing, reply correlation, and the caller timeout', async () => {
  const { coordinator } = harness();
  const requestHeader = streamProtocol.encodeStreamHeader({
    kind: streamProtocol.ZLinkStreamMessageKind.Request,
    codec: streamProtocol.ZLinkStreamCodec.Json,
    flags: streamProtocol.ZLinkStreamHeaderFlags.None,
    requestSeq: 731n,
    name: 'HandoffRequest',
    metadata: new Map([['trace-id', 'handoff-731']]),
    correlationId: 'caller-731'
  });
  const parts = [
    zlink.Message.from(Buffer.from(requestHeader)),
    zlink.Message.from(Buffer.from(JSON.stringify({ marker: 'R1' })))
  ];

  coordinator.begin('actor-1', 1n);
  const pendingReply = coordinator.capture('actor-1', parts, true, undefined, actorRef());
  parts.forEach((part) => part.close());
  const backlog = coordinator.snapshot('actor-1');
  const replayHeader = streamProtocol.decodeStreamHeader(Buffer.from(backlog[0].header, 'base64'));

  assert.equal(backlog[0].returnResponse, true);
  assert.equal(replayHeader.kind, streamProtocol.ZLinkStreamMessageKind.Request);
  assert.equal(replayHeader.requestSeq, 731n);
  assert.equal(replayHeader.correlationId, 'caller-731');
  assert.equal(replayHeader.metadata.get('trace-id'), 'handoff-731');
  assert.notEqual(replayHeader.flags & streamProtocol.ZLinkStreamHeaderFlags.HasRequestSeq, 0);
  assert.notEqual(replayHeader.flags & streamProtocol.ZLinkStreamHeaderFlags.HasMetadata, 0);

  const terminal = {
    index: backlog[0].index,
    ok: true,
    response: { marker: 'R1', requestSeq: '731' }
  };
  coordinator.complete('actor-1', target(), targetActorRef(), [terminal]);
  assert.equal(
    coordinator.acceptRelocatedTerminal('actor-1', backlog[0], terminal, 'target-node', 2n),
    'terminalReceived'
  );
  assert.deepEqual(await pendingReply, { marker: 'R1', requestSeq: '731' });

  coordinator.begin('actor-1', 2n);
  const lateParts = [
    zlink.Message.from(Buffer.from(requestHeader)),
    zlink.Message.from(Buffer.from(JSON.stringify({ marker: 'late' })))
  ];
  const lateReply = coordinator.capture('actor-1', lateParts, true, undefined, actorRef(2n));
  lateParts.forEach((part) => part.close());
  const lateBacklog = coordinator.snapshot('actor-1');
  const caller = Promise.race([
    lateReply,
    new Promise((_resolve, reject) => setTimeout(() => reject(new Error('normal request timeout')), 5))
  ]);
  await assert.rejects(caller, /normal request timeout/);
  const lateTerminal = {
    index: lateBacklog[0].index,
    ok: true,
    response: { marker: 'late' }
  };
  coordinator.complete('actor-1', target('late'), targetActorRef('late', 3n), [lateTerminal]);
  assert.equal(
    coordinator.acceptRelocatedTerminal(
      'actor-1', lateBacklog[0], lateTerminal, 'late-node', 2n
    ),
    'terminalReceived'
  );
  assert.deepEqual(await lateReply, { marker: 'late' });
});
