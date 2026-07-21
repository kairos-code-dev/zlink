const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');

function frame(value) {
  return [zlink.Message.from(`header:${value}`), zlink.Message.from(value)];
}

function actorRef(generation = 1n) {
  return { nodeRid: zlink.RoutingId.from('source'), actorId: 'actor-1', generation };
}

function target(name = 'target') {
  return {
    routerChannelId: 'mesh',
    targetNodeRid: zlink.RoutingId.from(`${name}-node`),
    spotRid: zlink.RoutingId.from(`${name}-spot`),
    spotKind: framework.ZLinkSpotKind.User
  };
}

function targetActorRef(name = 'target', generation = 2n) {
  return { nodeRid: zlink.RoutingId.from(`${name}-node`), actorId: 'actor-1', generation };
}

function harness(forwardWindowMs = 30) {
  const forwarded = [];
  const markers = [];
  let currentGeneration = 2n;
  const transport = {
    async sendToSpot(_target, payload) {
      forwarded.push(Buffer.from(payload.payload, 'base64').toString());
    },
    async requestToSpot() {
      return { ok: true };
    }
  };
  const coordinator = new framework.ZLinkActorHandoffCoordinator({
    routedTransport: transport,
    forwardWindowMs,
    isStaleActorRef: (_actorId, ref) => ref.generation !== currentGeneration,
    isCurrentHandoffTarget: (_actorId, spotRid) => spotRid === 'target-spot',
    onMarker: (marker, actorId, index) => markers.push({ marker, actorId, index })
  });
  return {
    coordinator,
    forwarded,
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

test('packets captured after the commit snapshot are forwarded only after backlog completion', async () => {
  const { coordinator, forwarded } = harness();
  coordinator.begin('actor-1', 1n);
  const backlogParts = frame('B1');
  await coordinator.capture('actor-1', backlogParts, false, undefined, actorRef());
  backlogParts.forEach((part) => part.close());
  const backlog = coordinator.snapshot('actor-1');

  const directParts = frame('D1');
  await coordinator.capture('actor-1', directParts, false, undefined, actorRef());
  directParts.forEach((part) => part.close());
  assert.deepEqual(forwarded, []);

  coordinator.complete(
    'actor-1',
    target(),
    targetActorRef(),
    backlog.map((packet) => ({ index: packet.index, ok: true }))
  );
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(forwarded, ['D1']);
});

test('bound-session packets keep one sequence across snapshot and forwarding activation', async () => {
  const { coordinator, forwarded } = harness();
  const sessionTarget = {
    routerChannelId: 'session-mesh',
    targetNodeRid: zlink.RoutingId.from('session-node'),
    spotRid: zlink.RoutingId.from('session-spot')
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
    backlog.map((packet) => Buffer.from(packet.payload, 'base64').toString()).concat(forwarded),
    ['S1', 'S2', 'S3', 'S4']
  );
  assert.equal(backlog[0].remoteBoundSessionTarget.routerChannelId, 'session-mesh');
});

test('stragglers forward inside the window and fail fast after mapping eviction', async () => {
  const { coordinator, forwarded, markers } = harness(10);
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const inside = frame('G1');
  await coordinator.capture('actor-1', inside, false, undefined, actorRef(1n));
  inside.forEach((part) => part.close());
  assert.deepEqual(forwarded, ['G1']);

  await new Promise((resolve) => setTimeout(resolve, 20));
  const outside = frame('G2');
  await assert.rejects(
    coordinator.capture('actor-1', outside, false, undefined, actorRef(1n)),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorLocationStale
  );
  outside.forEach((part) => part.close());
  assert.equal(markers.some((entry) => entry.marker === 'mapping_evicted'), true);
  assert.equal(markers.some((entry) => entry.marker === 'stale_fail_fast'), true);
});

test('a Core-routed packet owned by the current actor bypasses an older forwarding mapping', async () => {
  const { coordinator, forwarded, markers } = harness();
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const current = frame('current-owner');
  assert.equal(
    coordinator.capture('actor-1', current, false, undefined, actorRef(2n)),
    undefined
  );
  current.forEach((part) => part.close());

  assert.deepEqual(forwarded, []);
  assert.equal(markers.some((entry) => entry.marker === 'straggler_forward'), false);
});

test('a Core-routed packet marked with the current target bypasses stale owner generation', async () => {
  const { coordinator, forwarded, markers } = harness();
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target(), targetActorRef());

  const owner = Object.assign(actorRef(1n), {
    handoffForwarded: true,
    handoffTargetSpotRid: 'target-spot'
  });
  const current = frame('current-target');
  assert.equal(
    coordinator.capture('actor-1', current, false, undefined, owner),
    undefined
  );
  current.forEach((part) => part.close());

  assert.deepEqual(forwarded, []);
  assert.equal(markers.some((entry) => entry.marker === 'straggler_forward'), false);
});

test('chained movement replaces the node-local mapping and leaves no retained entry after cutoff', async () => {
  const { coordinator, forwarded } = harness(10);
  coordinator.begin('actor-1', 1n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target('first'), targetActorRef('first'));
  assert.equal(coordinator.forwardingCount('actor-1'), 1);

  coordinator.begin('actor-1', 2n);
  coordinator.snapshot('actor-1');
  coordinator.complete('actor-1', target('second'), targetActorRef('second', 3n));
  assert.equal(coordinator.forwardingCount('actor-1'), 1);
  const packet = frame('chain');
  await coordinator.capture('actor-1', packet, false, undefined, actorRef(2n));
  packet.forEach((part) => part.close());
  assert.deepEqual(forwarded, ['chain']);

  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.equal(coordinator.forwardingCount('actor-1'), 0);
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

  coordinator.complete('actor-1', target(), targetActorRef(), [{
    index: backlog[0].index,
    ok: true,
    response: { marker: 'R1', requestSeq: '731' }
  }]);
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
  coordinator.complete('actor-1', target('late'), targetActorRef('late', 3n), [{
    index: lateBacklog[0].index,
    ok: true,
    response: { marker: 'late' }
  }]);
  assert.deepEqual(await lateReply, { marker: 'late' });
});
