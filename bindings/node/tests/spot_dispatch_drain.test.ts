// SPDX-License-Identifier: MPL-2.0

'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const { spawn } = require('node:child_process');
const path = require('node:path');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise<void>((resolve, reject) =>
    server.close((error) => error ? reject(error) : resolve()));
  return port;
}

function applicationMetadata(key: string, value: string): Buffer {
  const keyBytes = Buffer.from(key);
  const valueBytes = Buffer.from(value);
  const valueLength = Buffer.alloc(2);
  valueLength.writeUInt16BE(valueBytes.length);
  return Buffer.concat([
    Buffer.from([1, 1, keyBytes.length]),
    keyBytes,
    valueLength,
    valueBytes
  ]);
}

function closeRecordParts(records) {
  for (const record of records) {
    for (const part of record.parts) {
      part.close();
    }
  }
}

async function takeMatchingRecord(node, domains, label, predicate, consume) {
  const ready = node.createReadyBatch(16);
  const receive = node.createReceiveBatch(16, 64, 1 << 16);
  const deadline = Date.now() + 5000;
  try {
    while (Date.now() < deadline) {
      ready.reset();
      const drained = node.drainReady(
        domains,
        ready,
        zlink.RecvFlags.DontWait
      );
      if (!drained.ok || drained.records.length === 0) {
        await new Promise((resolve) => setTimeout(resolve, 10));
        continue;
      }

      for (let index = 0; index < drained.records.length; index += 1) {
        const claim = ready.takeClaim(index);
        let outcome;
        try {
          receive.reset();
          outcome = claim.recvBatch(receive, zlink.RecvFlags.DontWait);
        } finally {
          claim.release();
        }
        if (!outcome.ok) {
          continue;
        }

        try {
          for (const record of outcome.records) {
            if (predicate(record)) {
              return consume(record);
            }
          }
        } finally {
          closeRecordParts(outcome.records);
        }
      }
    }
  } finally {
    receive.close();
    ready.close();
  }
  throw new Error(`${label} timed out`);
}

function sameOperation(left, right) {
  return left.high === right.high && left.low === right.low;
}

async function createStartedNode(label) {
  const meshName = `spot-dispatch-${label}`;
  const channelName = 'events';
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const rid = zlink.RoutingId.from(Buffer.from(`${label}-node`));
  const context = zlink.createContext();
  const node = zlink.createMeshNode(context, { meshName });

  node.setRoutingId(rid);
  node.setBind(endpoint);
  node.addChannelName(channelName);
  node.start();

  return {
    channelName,
    node,
    context,
    close() {
      node.close();
      context.close();
    }
  };
}

test('logical multicast is drained through a user spot claim', async () => {
  const runtime = await createStartedNode('multicast');
  const publisher = runtime.node.createPublisher();
  const subscriberRid = zlink.RoutingId.from(Buffer.from('multicast-user-spot'));
  const { spot: subscriber } = runtime.node.getOrCreateSpot(subscriberRid);

  try {
    subscriber.setSubscription(runtime.channelName, 'dispatch-drain');
    const detail = publisher.publish(
      runtime.channelName,
      'dispatch-drain',
      Buffer.from('dispatch-payload'),
      { flags: zlink.SendFlags.DontWait }
    );
    assert.equal(detail.admittedLocalSpotCount, 1);

    const received = await takeMatchingRecord(
      runtime.node,
      zlink.ReadyDomain.Application,
      'user spot multicast',
      (record) => record.kind === zlink.ReceiveKind.SpotMulticast,
      (record) => ({
        topic: record.topic,
        parts: record.parts.map((part) => part.data().toString())
      })
    );
    assert.equal(received.topic, 'dispatch-drain');
    assert.deepEqual(received.parts, ['dispatch-payload']);
  } finally {
    subscriber.close();
    publisher.close();
    runtime.close();
  }
});

test('async logical multicast owns inputs and permits close while work completes', async () => {
  const runtime = await createStartedNode('async-multicast');
  const publisher = runtime.node.createPublisher();
  const subscriber = runtime.node.entrySpot();

  try {
    subscriber.setSubscription(runtime.channelName, 'async-dispatch-drain');
    const payload = Buffer.from('owned-before-worker');
    const metadata = applicationMetadata('source', 'owned-metadata');
    const expectedMetadata = Buffer.from(metadata);
    const pending = publisher.publishAsync(
      runtime.channelName,
      'async-dispatch-drain',
      payload,
      { metadata }
    );
    payload.fill(0);
    metadata.fill(0);
    publisher.close();

    const outcome = await pending;
    assert.equal(outcome.result, zlink.SubmitResult.Ok);
    assert.equal(outcome.detail.admittedLocalSpotCount, 1);
    const received = await takeMatchingRecord(
      runtime.node,
      zlink.ReadyDomain.Application,
      'async logical multicast',
      (record) => record.kind === zlink.ReceiveKind.SpotMulticast,
      (record) => ({
        sourceBindingGeneration: record.sourceBindingGeneration,
        metadata: record.applicationMetadata,
        parts: record.parts.map((part) => part.data().toString())
      })
    );
    assert.equal(received.sourceBindingGeneration, 0n);
    assert.deepEqual(received.metadata, expectedMetadata);
    assert.deepEqual(received.parts, ['owned-before-worker']);
  } finally {
    subscriber.close();
    publisher.close();
    runtime.close();
  }
});

test('pre-aborted async logical multicast does not start Core publish', async () => {
  const runtime = await createStartedNode('async-pre-abort');
  const publisher = runtime.node.createPublisher();
  const controller = new AbortController();
  const reason = new Error('cancel before worker start');
  controller.abort(reason);

  try {
    await assert.rejects(
      publisher.publishAsync(runtime.channelName, 'cancelled', Buffer.from('ignored'), undefined, controller.signal),
      (error) => error === reason
    );
    assert.equal(runtime.node.status().pendingApplicationMessages, 0n);
  } finally {
    publisher.close();
    runtime.close();
  }
});

test('async logical multicast returns NotFound as an outcome with Core detail', async () => {
  const runtime = await createStartedNode('async-not-found');
  const publisher = runtime.node.createPublisher();

  try {
    const outcome = await publisher.publishAsync(
      runtime.channelName,
      'no-subscriber',
      Buffer.from('not-found')
    );
    assert.equal(outcome.result, zlink.SubmitResult.NotFound);
    assert.deepEqual(outcome.detail, {
      snapshotRemoteTargetCount: 0,
      admittedRemoteTargetCount: 0,
      droppedRemoteTargetCount: 0,
      unreachableRemoteTargetCount: 0,
      snapshotLocalSpotCount: 0,
      admittedLocalSpotCount: 0,
      droppedLocalSpotCount: 0
    });
  } finally {
    publisher.close();
    runtime.close();
  }
});

test('async publisher close retains native state without occupying the JS event loop', async () => {
  const fixture = path.join(__dirname, 'fixtures', 'mesh_publisher_close_gc_child.js');
  const child = spawn(process.execPath, ['--expose-gc', fixture], {
    env: { ...process.env, UV_THREADPOOL_SIZE: '1' },
    stdio: ['ignore', 'pipe', 'pipe']
  });
  let stdout = '';
  let stderr = '';
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', (chunk) => { stdout += chunk; });
  child.stderr.on('data', (chunk) => { stderr += chunk; });
  const [code, signal] = await once(child, 'exit');

  assert.equal(signal, null, stderr);
  assert.equal(code, 0, stderr);
  assert.match(stdout, /publisher-close-gc-ok/);
});

test('queued async logical multicast can abort before its Core call starts', async () => {
  const fixture = path.join(__dirname, 'fixtures', 'mesh_publisher_cancel_child.js');
  const child = spawn(process.execPath, [fixture], {
    env: { ...process.env, UV_THREADPOOL_SIZE: '1' },
    stdio: ['ignore', 'pipe', 'pipe']
  });
  let stdout = '';
  let stderr = '';
  child.stdout.setEncoding('utf8');
  child.stderr.setEncoding('utf8');
  child.stdout.on('data', (chunk) => { stdout += chunk; });
  child.stderr.on('data', (chunk) => { stderr += chunk; });
  const [code, signal] = await once(child, 'exit');

  assert.equal(signal, null, stderr);
  assert.equal(code, 0, stderr);
  assert.match(stdout, /publisher-cancel-ok/);
});

test('entry spot drains multipart logical multicast', async () => {
  const runtime = await createStartedNode('entry-multicast');
  const publisher = runtime.node.createPublisher();
  const subscriber = runtime.node.entrySpot();

  try {
    subscriber.setSubscription(runtime.channelName, 'entry-dispatch-drain');
    const detail = publisher.publish(
      runtime.channelName,
      'entry-dispatch-drain',
      [Buffer.from('header'), Buffer.from('payload')],
      { flags: zlink.SendFlags.DontWait }
    );
    assert.equal(detail.admittedLocalSpotCount, 1);

    const received = await takeMatchingRecord(
      runtime.node,
      zlink.ReadyDomain.Application,
      'entry spot multicast',
      (record) => record.kind === zlink.ReceiveKind.SpotMulticast,
      (record) => ({
        topic: record.topic,
        parts: record.parts.map((part) => part.data().toString())
      })
    );
    assert.equal(received.topic, 'entry-dispatch-drain');
    assert.deepEqual(received.parts, ['header', 'payload']);
  } finally {
    subscriber.close();
    publisher.close();
    runtime.close();
  }
});

test('spot request is replied through the claimed receive record', async () => {
  const runtime = await createStartedNode('request');
  const requester = runtime.node.entrySpot();
  const responderRid = zlink.RoutingId.from(Buffer.from('request-user-spot'));
  const { spot: responder } = runtime.node.getOrCreateSpot(responderRid);

  try {
    const responderStatus = responder.status();
    const operation = requester.requestToSpot(
      runtime.node.status().routingId,
      responderStatus.routingId,
      responderStatus.lifecycleGeneration,
      Buffer.from('spot-routed-body'),
      { timeoutMs: 2000 }
    );

    const request = await takeMatchingRecord(
      runtime.node,
      zlink.ReadyDomain.Application,
      'spot request',
      (record) => record.kind === zlink.ReceiveKind.SpotRequest,
      (record) => {
        const requestParts = record.parts.map((part) => part.data().toString());
        const submit = record.reply(Buffer.from('spot-routed-reply'));
        return { requestParts, submit };
      }
    );
    assert.deepEqual(request.requestParts, ['spot-routed-body']);
    assert.equal(request.submit, zlink.SubmitResult.Ok);

    const completion = await takeMatchingRecord(
      runtime.node,
      zlink.ReadyDomain.Infrastructure,
      'spot request completion',
      (record) =>
        record.kind === zlink.ReceiveKind.Completion
        && sameOperation(record.operationId, operation),
      (record) => ({
        terminalResult: record.terminalResult,
        parts: record.parts.map((part) => part.data().toString())
      })
    );
    assert.equal(completion.terminalResult, zlink.RequestResult.Ok);
    assert.deepEqual(completion.parts, ['spot-routed-reply']);
  } finally {
    responder.close();
    requester.close();
    runtime.close();
  }
});

test('actor creation payload is surfaced as entry-spot control data', async () => {
  const ctx = zlink.createContext();
  const node = zlink.createMeshNode(ctx, { meshName: 'spot-dispatch-actor-create' });
  const rid = zlink.RoutingId.from(Buffer.from('actor-create-node'));
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  node.setRoutingId(rid);
  node.setBind(endpoint);
  node.addChannelName('actors');
  node.start();
  const entrySpot = node.entrySpot();

  try {
    const actor = node.createActor('node-create-payload', [
      Buffer.from('profile'),
      Buffer.from('display-name')
    ]);

    const event = await takeMatchingRecord(
      node,
      zlink.ReadyDomain.Application,
      'entry spot actor create',
      (record) =>
        record.kind === zlink.ReceiveKind.SpotControl
        && record.kindData?.kind === 'actorControl',
      (record) => ({
        control: record.kindData,
        parts: record.parts.map((part) => part.data().toString())
      })
    );
    assert.equal(event.control.lifecycleKind, zlink.ActorLifecycleKind.Created);
    assert.equal(event.control.currentActor.actorId, actor.actorId);
    assert.deepEqual(event.parts, ['profile', 'display-name']);
  } finally {
    entrySpot.close();
    node.close();
    ctx.close();
  }
});
