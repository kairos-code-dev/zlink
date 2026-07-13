'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');
const connector = require('../../packages/stream-connector/dist');

test('DRAIN-001/012 drain is shared, changes readiness, and publishes terminal state', async () => {
  const events = [];
  const publisher = {
    register() {},
    async publish(event) { events.push(event); }
  };
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration(),
    runtimeEventPublisher: publisher
  });
  const waiting = host.awaitDrained();
  const first = host.drain(1000);
  const second = host.drain(10);
  assert.equal(host.isReady(), false);
  assert.deepEqual(await first, { kind: 'drained' });
  assert.deepEqual(await second, { kind: 'drained' });
  assert.deepEqual(await waiting, { kind: 'drained' });
  assert.deepEqual(events.map((event) => event.state), ['Draining', 'Drained']);
  assert(events.every((event) => event.sourceName === 'drain'));
});

test('DRAIN-007 waiter cancellation does not cancel shared drain', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  const controller = new AbortController();
  controller.abort(new Error('wait canceled'));
  await assert.rejects(() => host.drain(1000, controller.signal), /wait canceled/);
  assert.deepEqual(await host.awaitDrained(), { kind: 'drained' });
});

test('DRAIN-006 rejects a non-positive deadline before starting', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  await assert.rejects(() => host.drain(0), /greater than zero/);
  assert.equal(host.isReady(), true);
});

test('DRAIN-018 managed stream writes session-closing before disconnecting peer', async () => {
  const order = [];
  let frame;
  const stream = new framework.ZLinkManagedStream({
    send(_routingId, message) {
      order.push('send');
      frame = Uint8Array.from(message.data());
      return true;
    },
    disconnectPeer() { order.push('disconnect'); }
  }, 'session-1');
  await stream.closeForDrain();
  assert.deepEqual(order, ['send', 'disconnect']);
  const decodedFrame = connector.ZlinkStreamFrameCodec.decode(frame);
  const header = connector.ZlinkStreamHeaderCodec.decode(decodedFrame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Control);
  assert.equal(header.name, 'session-closing');
  assert.deepEqual([...decodedFrame.payload], [1, 4, 0, 12, ...Buffer.from('server drain')]);
});

test('DRAIN-013 DrainNatural waits for user spots without forcing close', async () => {
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{ name: 'rooms', drainPolicy: 'DrainNatural' }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  let polls = 0;
  let closes = 0;
  host.setSpotManager({
    async list() { return polls++ === 0 ? [{ spotRid: 'room-1' }] : []; },
    async close() { closes += 1; return true; }
  });
  assert.deepEqual(await host.drain(1000), { kind: 'drained' });
  assert.equal(closes, 0);
});

test('DRAIN-014 ReleaseAndRecreate closes existing user spots', async () => {
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{ name: 'rooms', drainPolicy: 'ReleaseAndRecreate' }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  let active = true;
  const closed = [];
  host.setSpotManager({
    async list() { return active ? [{ spotRid: 'room-1' }] : []; },
    async close(spotRid) { closed.push(spotRid); active = false; return true; }
  });
  assert.deepEqual(await host.drain(1000), { kind: 'drained' });
  assert.deepEqual(closed, ['room-1']);
});
