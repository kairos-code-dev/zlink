const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');
const backend = require('../../packages/framework/dist/runtime/backend');

test('Channel and fanout HWM count only the application payload frame', async () => {
  const observed = [];
  const budget = {
    enqueue(bytes) { observed.push(bytes); },
    start() {},
    complete() {}
  };
  const header = message(4096);
  const payload = message(7);

  const channel = new framework.ZLinkChannelReceiveLoop(
    'api',
    {},
    { async dispatch() {} },
    undefined,
    undefined,
    budget
  );
  await channel.dispatchAndClose(received([header, payload]));

  const fanout = new framework.ZLinkSubscriberReceiveLoop(
    { createReadablePoller() { return { wait() {}, dispose() {} }; } },
    {},
    { async dispatch() {} },
    undefined,
    budget
  );
  await fanout.dispatchAndClose({ topic: 'orders', parts: [header, payload] });

  assert.deepEqual(observed, [7n, 7n]);
});

test('Channel request permit cancellation releases queued bytes and received ownership', async () => {
  const cancelled = new Error('cancelled');
  let queuedBytes = 0n;
  let closeCount = 0;
  let dispatched = false;
  const budget = rejectingBudget(cancelled, (bytes) => { queuedBytes = bytes; });
  const channel = new framework.ZLinkChannelReceiveLoop(
    'api',
    {},
    { async dispatch() { dispatched = true; } },
    undefined,
    undefined,
    budget
  );
  const packet = received([message(4), message(11)], 1n, () => { closeCount += 1; });

  await assert.rejects(channel.dispatchAndClose(packet), cancelled);

  assert.equal(queuedBytes, 0n);
  assert.equal(closeCount, 1);
  assert.equal(dispatched, false);
});

test('Mesh request permit rejection releases queued bytes and message parts', async () => {
  const rejected = new Error('permit rejected');
  let queuedBytes = 0n;
  let partCloseCount = 0;
  let readyHandler;
  let reportedError;
  const errorReported = new Promise((resolve) => { reportedError = resolve; });
  const part = message(13, () => { partCloseCount += 1; });
  let received = false;
  const claim = {
    recvBatch() {
      if (received) return { ok: false, records: [] };
      received = true;
      return {
        ok: true,
        records: [{ operationKind: 1, parts: [part] }]
      };
    },
    release() {}
  };
  const node = {
    setReadyHandler(handler) { readyHandler = handler; },
    createReadyBatch() {
      return { reset() {}, takeClaim() { return claim; }, close() {} };
    },
    createReceiveBatch() {
      return { reset() {}, close() {} };
    },
    drainReady() {
      return {
        ok: true,
        hasResidue: false,
        records: [{ ownerKind: framework.ReadyOwnerKind.Node }]
      };
    }
  };
  const pump = new backend.ZLinkMeshDispatchPump(node, {
    inboundDispatchBudget: rejectingBudget(
      rejected,
      (bytes) => { queuedBytes = bytes; }
    ),
    dispatch() {
      assert.fail('dispatch must not run without a completion permit');
    },
    reportError(error) {
      reportedError(error);
    }
  });

  try {
    pump.start();
    readyHandler(framework.ReadyDomain.Application);
    assert.equal(await errorReported, rejected);
    assert.equal(queuedBytes, 0n);
    assert.equal(partCloseCount, 1);
  } finally {
    await pump.dispose();
  }
});

function message(size, onClose = () => {}) {
  return {
    data() { return Buffer.alloc(size); },
    size() { return size; },
    close: onClose
  };
}

function received(parts, requestSeq = null, onClose = () => {}) {
  return {
    parts,
    routingId: 'peer',
    requestSeq,
    close: onClose
  };
}

function rejectingBudget(error, observeQueued) {
  let queuedBytes = 0n;
  const publish = () => observeQueued(queuedBytes);
  return {
    get receivePaused() { return false; },
    enqueue(bytes) {
      queuedBytes += bytes;
      publish();
    },
    start(bytes) {
      queuedBytes -= bytes;
      publish();
    },
    cancelQueued(bytes) {
      queuedBytes -= bytes;
      publish();
    },
    complete() {},
    async acquireCompletionSend() {
      throw error;
    },
    onResume() {
      return () => {};
    }
  };
}
