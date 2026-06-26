const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');

class PlayerActor {
  constructor(actorId, context) {
    this.actorId = actorId;
    this.context = context;
  }
}

class PlayerFactory {
  create(actorId, context) {
    return new PlayerActor(actorId, context);
  }
}

async function createEntryFixture(entrySpotType, packetHandlers = []) {
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]])
  });
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType,
    nativeSpot: { routingId: 'entry-test', async dispose() {} },
    nodeRid: 'node-test',
    spotNodeName: 'node-test'
  });
  await activation.create();
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime();
  for (const descriptor of packetHandlers) {
    registry.addPacket(descriptor);
  }
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry,
    spot: activation.entrySpot,
    serial: activation.serialExecutor
  });
  const router = new framework.ZLinkActorDispatchRouter(manager, {
    entryExecutor: activation.serialExecutor
  });
  return { manager, activation, registry, dispatcher, router };
}

function overlapTracker(events) {
  let active = 0;
  let overlapped = false;
  return {
    async run(name, body) {
      active += 1;
      overlapped = overlapped || active > 1;
      events.push(`${name}:start`);
      try {
        await body?.();
      } finally {
        events.push(`${name}:end`);
        active -= 1;
      }
    },
    get overlapped() {
      return overlapped;
    }
  };
}

function delay(durationMs) {
  return new Promise((resolve) => setTimeout(resolve, durationMs));
}

test('entry spot callbacks from mixed setImmediate/queueMicrotask backend callbacks keep enqueue order without overlap', async () => {
  const events = [];
  const tracker = overlapTracker(events);
  class AdmissionEntrySpot {
    onCreateActor(actor) {
      return tracker.run(`create:${actor.actorId}`, () => delay(2));
    }
  }
  class MovePacketHandler {
    handle(_spot, actor, _context, message) {
      return tracker.run(`packet:${actor.actorId}:${message}`, () => delay(1));
    }
  }
  const fixture = await createEntryFixture(AdmissionEntrySpot, [{
    kind: framework.ZLinkActorPacketKind.Send,
    packetName: 'move',
    actorType: PlayerActor,
    handlerType: MovePacketHandler
  }]);
  const alice = await fixture.manager.getOrCreateActor('alice', 'player');
  const bob = await fixture.manager.getOrCreateActor('bob', 'player');

  // Native/binding callbacks arrive from mixed macrotask/microtask contexts;
  // each enqueues into the Entry Spot serial executor instead of invoking
  // user code directly.
  const submissions = [];
  const enqueueOrder = [];
  const fromBackend = (schedule, name, dispatch) => new Promise((resolve) => {
    schedule(() => {
      enqueueOrder.push(name);
      submissions.push(dispatch());
      resolve();
    });
  });
  await fromBackend(queueMicrotask, 'packet:alice:m1', () =>
    fixture.router.submit('alice', (snapshot) =>
      fixture.dispatcher.dispatchSend(snapshot.actor, 'move', 'm1')));
  await fromBackend(setImmediate, 'create:bob', () =>
    fixture.activation.notifyCreateActor(bob, framework.ZLinkMessage.from({})));
  await fromBackend(queueMicrotask, 'packet:bob:m2', () =>
    fixture.router.submit('bob', (snapshot) =>
      fixture.dispatcher.dispatchSend(snapshot.actor, 'move', 'm2')));
  await fromBackend(setImmediate, 'packet:alice:m3', () =>
    fixture.router.submit('alice', (snapshot) =>
      fixture.dispatcher.dispatchSend(snapshot.actor, 'move', 'm3')));
  await Promise.all(submissions);

  assert.equal(tracker.overlapped, false);
  const startOrder = events
    .filter((event) => event.endsWith(':start'))
    .map((event) => event.slice(0, -':start'.length));
  assert.deepEqual(startOrder, enqueueOrder);
  for (let index = 0; index < events.length; index += 2) {
    assert.equal(events[index].endsWith(':start'), true);
    assert.equal(events[index + 1], events[index].replace(':start', ':end'));
  }
  assert.equal(alice instanceof PlayerActor, true);
});

test('entry spot does not start the next callback before the previous handler promise settles', async () => {
  const events = [];
  let releaseFirst;
  const firstGate = new Promise((resolve) => {
    releaseFirst = resolve;
  });
  class GatedEntrySpot {
    async onCreateActor(actor) {
      events.push(`create:${actor.actorId}:start`);
      await firstGate;
      events.push(`create:${actor.actorId}:settled`);
    }
  }
  class PingHandler {
    async handle(_spot, actor) {
      events.push(`packet:${actor.actorId}`);
    }
  }
  const fixture = await createEntryFixture(GatedEntrySpot, [{
    kind: framework.ZLinkActorPacketKind.Send,
    packetName: 'ping',
    actorType: PlayerActor,
    handlerType: PingHandler
  }]);
  const alice = await fixture.manager.getOrCreateActor('alice', 'player');

  const first = fixture.activation.notifyCreateActor(alice, framework.ZLinkMessage.from({}));
  const second = fixture.router.submit('alice', (snapshot) =>
    fixture.dispatcher.dispatchSend(snapshot.actor, 'ping', 'p'));
  await delay(10);
  assert.deepEqual(events, ['create:alice:start']);

  releaseFirst();
  await Promise.all([first, second]);
  assert.deepEqual(events, ['create:alice:start', 'create:alice:settled', 'packet:alice']);
});

test('entry spot timer ticks and actor packets share one serial line', async () => {
  const events = [];
  const tracker = overlapTracker(events);
  class TimerEntrySpot {}
  class TickHandler {
    handle() {
      return tracker.run('tick');
    }
  }
  class SlowPacketHandler {
    handle() {
      return tracker.run('packet', () => delay(40));
    }
  }
  const fixture = await createEntryFixture(TimerEntrySpot, [{
    kind: framework.ZLinkActorPacketKind.Send,
    packetName: 'slow',
    actorType: PlayerActor,
    handlerType: SlowPacketHandler
  }]);
  await fixture.manager.create('alice', 'player');

  const timer = await fixture.activation.context.addTimer('entry-tick', 5, TickHandler);
  const packet = fixture.router.submit('alice', (snapshot) =>
    fixture.dispatcher.dispatchSend(snapshot.actor, 'slow', 's'));
  await packet;
  await delay(15);
  await timer.cancel();

  assert.equal(tracker.overlapped, false);
  assert.equal(events.filter((event) => event === 'tick:start').length > 0, true);
});

test('joined user spot actors keep per-actor mailbox dispatch off the entry line', async () => {
  class IdleEntrySpot {}
  const fixture = await createEntryFixture(IdleEntrySpot);
  await fixture.manager.create('alice', 'player');
  fixture.manager.getState('alice').setJoinedSpot('stage-1', { context: { spotRid: 'stage-1' } });

  let releaseEntry;
  const entryGate = new Promise((resolve) => {
    releaseEntry = resolve;
  });
  const entryBusy = fixture.activation.serialExecutor.execute(() => entryGate);

  // A user-Spot-joined actor must proceed while the Entry Spot line is busy.
  const joined = await fixture.router.submit('alice', (snapshot) => `spot:${snapshot.spotRid}`);
  assert.equal(joined, 'spot:stage-1');
  releaseEntry();
  await entryBusy;
});

test('detached request continuation re-enters the entry spot serial line', async () => {
  const events = [];
  let resolveReply;
  const channelClient = {
    requestToChannel() {
      return {
        packetName() { return this; },
        timeout() { return this; },
        submit() {
          events.push('request:submitted');
          return new Promise((resolve) => {
            resolveReply = resolve;
          });
        }
      };
    },
    sendToChannel() { throw new Error('not used'); }
  };
  class DetachedEntrySpot {}
  const fixture = await createEntryFixture(DetachedEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const outbound = new framework.DefaultZLinkSpotOutbound(serial, channelClient);

  // Detached path: the continuation is registered outside the handler turn.
  const continuation = outbound.requestToChannel('api', 'ping').submit().then((reply) => {
    events.push(`continuation:${reply}:executing=${serial.isExecuting}`);
  });
  await delay(5);
  assert.deepEqual(events, ['request:submitted']);

  // While the reply is delivered, a competing entry callback is queued; the
  // continuation must enter the serial line, not run as a bare microtask.
  let releaseBusy;
  const busyGate = new Promise((resolve) => {
    releaseBusy = resolve;
  });
  const busy = serial.execute(async () => {
    events.push('busy:start');
    await busyGate;
    events.push('busy:end');
  });
  await delay(2);
  resolveReply('pong');
  await delay(5);
  // The busy turn holds the line, so the continuation has not run yet.
  assert.deepEqual(events, ['request:submitted', 'busy:start']);
  releaseBusy();
  await Promise.all([busy, continuation]);
  assert.deepEqual(events, [
    'request:submitted',
    'busy:start',
    'busy:end',
    'continuation:pong:executing=true'
  ]);
});

test('gated request await inside an entry turn completes without overlapping the next callback', async () => {
  const events = [];
  const channelClient = {
    requestToChannel(_channelName, request) {
      return {
        packetName() { return this; },
        timeout() { return this; },
        async submit() {
          events.push(`request:${request}`);
          await delay(10);
          return `reply:${request}`;
        }
      };
    },
    sendToChannel() { throw new Error('not used'); }
  };
  class GatedEntrySpot {}
  const fixture = await createEntryFixture(GatedEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const outbound = new framework.DefaultZLinkSpotOutbound(serial, channelClient);

  const gated = serial.execute(async () => {
    events.push('handler:start');
    const reply = await outbound.requestToChannel('api', 'ping').submit();
    events.push(`handler:${reply}`);
  });
  const next = serial.execute(() => {
    events.push('next');
  });
  await Promise.all([gated, next]);

  assert.deepEqual(events, ['handler:start', 'request:ping', 'handler:reply:ping', 'next']);
});

test('yield request await inside an entry turn releases the serial line until reply resumes it', async () => {
  const events = [];
  let resolveReply;
  const channelClient = {
    requestToChannel(_channelName, request) {
      return {
        packetName() { return this; },
        timeout() { return this; },
        submit() {
          events.push(`request:${request}`);
          return new Promise((resolve) => {
            resolveReply = resolve;
          });
        }
      };
    },
    sendToChannel() { throw new Error('not used'); }
  };
  class YieldEntrySpot {}
  const fixture = await createEntryFixture(YieldEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const outbound = new framework.DefaultZLinkSpotOutbound(serial, channelClient);

  const yielded = serial.execute(async () => {
    events.push('handler:start');
    const reply = await outbound.requestToChannel('api', 'ping').yield();
    events.push(`handler:${reply}`);
  });
  const next = serial.execute(() => {
    events.push('next');
  });

  await delay(10);
  assert.deepEqual(events, ['handler:start', 'request:ping', 'next']);
  resolveReply('reply:ping');
  await Promise.all([yielded, next]);
  assert.deepEqual(events, ['handler:start', 'request:ping', 'next', 'handler:reply:ping']);
});

test('runWorker onCompleted callback re-enters the owning spot serial executor', async () => {
  class WorkerEntrySpot {}
  const fixture = await createEntryFixture(WorkerEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const context = fixture.activation.context;
  const events = [];

  let releaseBusy;
  const busyGate = new Promise((resolve) => {
    releaseBusy = resolve;
  });
  let completed;
  const completion = new Promise((resolve) => {
    completed = resolve;
  });

  const busy = serial.execute(async () => {
    events.push('busy:start');
    context.runWorker(() => {
      events.push('work');
      return 21 * 2;
    }).onCompleted((result) => {
      events.push(`completed:${result}:executing=${serial.isExecuting}`);
      completed();
    });
    await busyGate;
    events.push('busy:end');
  });
  // The work runs off the serial line, but the completion callback must not
  // run inline before the busy entry turn finishes.
  await delay(15);
  assert.deepEqual(events, ['busy:start', 'work']);
  releaseBusy();
  await Promise.all([busy, completion]);
  assert.deepEqual(events, ['busy:start', 'work', 'busy:end', 'completed:42:executing=true']);
});

test('runWorker submit supports the gated awaitable path inside a handler turn', async () => {
  class WorkerEntrySpot {}
  const fixture = await createEntryFixture(WorkerEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const context = fixture.activation.context;
  const events = [];

  const gated = serial.execute(async () => {
    events.push('handler:start');
    const result = await context.runWorker(async () => {
      await delay(5);
      return 'done';
    }).submit();
    events.push(`handler:${result}`);
  });
  const next = serial.execute(() => {
    events.push('next');
  });
  await Promise.all([gated, next]);

  assert.deepEqual(events, ['handler:start', 'handler:done', 'next']);
});

test('runWorker yield releases the current Spot turn until worker completion resumes it', async () => {
  class WorkerEntrySpot {}
  const fixture = await createEntryFixture(WorkerEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const context = fixture.activation.context;
  const events = [];
  let releaseWork;
  const workGate = new Promise((resolve) => {
    releaseWork = resolve;
  });

  const yielded = serial.execute(async () => {
    events.push('handler:start');
    const result = await context.runWorker(async () => {
      await workGate;
      return 'done';
    }).yield();
    events.push(`handler:${result}`);
  });
  const next = serial.execute(() => {
    events.push('next');
  });

  await delay(10);
  assert.deepEqual(events, ['handler:start', 'next']);
  releaseWork();
  await Promise.all([yielded, next]);
  assert.deepEqual(events, ['handler:start', 'next', 'handler:done']);
});

test('runWorker yield requires a Spot turn captured when the call is created', async () => {
  const worker = new framework.ZLinkSpotWorkerRuntime();
  const serial = new framework.ZLinkSpotSerialExecutor();
  const call = new framework.DefaultZLinkWorkerCall(worker, serial, () => 'done');

  await assert.rejects(
    () => call.yield(),
    (error) =>
      error instanceof framework.ZLinkConfigurationException
      && /captured when the call object was created/.test(error.message)
  );
});

test('runWorker queue full fails fast with WorkerQueueFull and does not block the dispatcher', async () => {
  const worker = new framework.ZLinkSpotWorkerRuntime({ maxThreads: 1, maxQueueLength: 1 });
  const serial = new framework.ZLinkSpotSerialExecutor();
  let releaseWork;
  const workGate = new Promise((resolve) => {
    releaseWork = resolve;
  });
  const longCall = new framework.DefaultZLinkWorkerCall(worker, serial, () => workGate);
  const longJob = longCall.submit();
  await delay(5);
  const queuedCall = new framework.DefaultZLinkWorkerCall(worker, serial, () => 'queued');
  const queuedJob = queuedCall.submit();

  const overflowCall = new framework.DefaultZLinkWorkerCall(worker, serial, () => 'overflow');
  const startedAt = Date.now();
  await assert.rejects(
    () => overflowCall.submit(),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.WorkerQueueFull
  );
  assert.equal(Date.now() - startedAt < 100, true);

  // The owning dispatcher keeps processing while the queue is saturated.
  const dispatched = await serial.execute(() => 'dispatcher-alive');
  assert.equal(dispatched, 'dispatcher-alive');

  // Queue-full through the callback terminator delivers onError on the
  // serial executor instead of throwing into the submitter.
  const errors = [];
  const overflowCallback = new framework.DefaultZLinkWorkerCall(worker, serial, () => 'overflow');
  overflowCallback.onCompleted(
    () => errors.push('completed'),
    (error) => errors.push(`error:${error.kind}:executing=${serial.isExecuting}`)
  );
  await delay(10);
  assert.deepEqual(errors, ['error:workerQueueFull:executing=true']);

  releaseWork('long');
  assert.equal(await longJob, 'long');
  assert.equal(await queuedJob, 'queued');
});

test('runWorker timeout fails the caller and drops the late completion without user callbacks', async () => {
  const worker = new framework.ZLinkSpotWorkerRuntime({ maxThreads: 2, maxQueueLength: 16 });
  const serial = new framework.ZLinkSpotSerialExecutor();
  const events = [];

  let observedSignal;
  const submitCall = new framework.DefaultZLinkWorkerCall(worker, serial, async (signal) => {
    observedSignal = signal;
    await delay(40);
    return 'late';
  }).timeoutMs(10);
  await assert.rejects(
    () => submitCall.submit(),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.WorkerTimedOut
  );
  assert.equal(observedSignal.aborted, true);

  const callbackCall = new framework.DefaultZLinkWorkerCall(worker, serial, async () => {
    await delay(40);
    return 'late';
  }).timeoutMs(10);
  callbackCall.onCompleted(
    (result) => events.push(`completed:${result}`),
    (error) => events.push(`error:${error.kind}`)
  );
  await delay(80);
  // The timeout error reached onError; the late completion fired no callback.
  assert.deepEqual(events, ['error:workerTimedOut']);
});

test('runWorker work failure surfaces as WorkerFailed wrapping the cause', async () => {
  const worker = new framework.ZLinkSpotWorkerRuntime();
  const serial = new framework.ZLinkSpotSerialExecutor();
  const cause = new Error('disk full');
  const call = new framework.DefaultZLinkWorkerCall(worker, serial, () => {
    throw cause;
  });
  await assert.rejects(
    () => call.submit(),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.WorkerFailed
      && error.cause === cause
  );
});

test('runWorker call accepts only one terminator', async () => {
  const worker = new framework.ZLinkSpotWorkerRuntime({ maxThreads: 1, maxQueueLength: 16 });
  const serial = new framework.ZLinkSpotSerialExecutor();

  const submitFirst = new framework.DefaultZLinkWorkerCall(worker, serial, () => 'done');
  const submitted = submitFirst.submit();
  assert.throws(
    () => submitFirst.onCompleted(() => {}),
    (error) =>
      error instanceof framework.ZLinkConfigurationException
      && /only one terminator/.test(error.message)
  );
  assert.equal(await submitted, 'done');

  const callbackFirst = new framework.DefaultZLinkWorkerCall(worker, serial, () => 'callback');
  let completed;
  const callbackCompleted = new Promise((resolve) => {
    completed = resolve;
  });
  callbackFirst.onCompleted((result) => {
    completed(result);
  });
  assert.throws(
    () => callbackFirst.submit(),
    (error) =>
      error instanceof framework.ZLinkConfigurationException
      && /only one terminator/.test(error.message)
  );
  assert.equal(await callbackCompleted, 'callback');
});

test('detached runWorker completion observes entry spot state mutated after submission', async () => {
  class AdmissionEntrySpot {
    constructor() {
      this.admissionEpoch = 1;
    }
  }
  const fixture = await createEntryFixture(AdmissionEntrySpot);
  const serial = fixture.activation.serialExecutor;
  const context = fixture.activation.context;
  const entrySpot = fixture.activation.entrySpot;
  const events = [];

  let completed;
  const completion = new Promise((resolve) => {
    completed = resolve;
  });
  await serial.execute(() => {
    const submittedEpoch = entrySpot.admissionEpoch;
    context.runWorker(async () => {
      await delay(10);
      return 'profile-loaded';
    }).onCompleted((result) => {
      // Detached path: state may have moved between submit and completion;
      // the callback observes the current state and re-validates.
      events.push(`completed:${result}:submitted=${submittedEpoch}:current=${entrySpot.admissionEpoch}`);
      completed();
    });
  });
  await serial.execute(() => {
    entrySpot.admissionEpoch = 2;
    events.push('epoch:2');
  });
  await completion;

  assert.deepEqual(events, ['epoch:2', 'completed:profile-loaded:submitted=1:current=2']);
});

test('worker options are accepted on the framework options builder and validated', () => {
  const options = framework.createFrameworkOptions((builder) => {
    builder.configureWorker({
      minThreads: 0,
      maxThreads: 8,
      idleTimeoutMs: 30_000,
      maxQueueLength: 1024
    });
  });
  assert.deepEqual(options.worker, {
    minThreads: 0,
    maxThreads: 8,
    idleTimeoutMs: 30_000,
    maxQueueLength: 1024
  });
  const registration = framework.createFrameworkRegistration(options);
  assert.deepEqual(registration.worker, options.worker);

  for (const invalid of [
    { maxThreads: 0 },
    { maxQueueLength: 0 },
    { minThreads: -1 },
    { idleTimeoutMs: -5 },
    { minThreads: 4, maxThreads: 2 }
  ]) {
    assert.throws(
      () => framework.createFrameworkRegistration({ worker: invalid }),
      framework.ZLinkConfigurationException
    );
  }
});

test('runWorker contract documentation does not claim CPU thread offload', () => {
  const declarationsRoot = path.resolve(__dirname, '../../packages/framework/dist/contracts');
  const declarations = readTree(declarationsRoot);
  const runWorkerDocs = [...declarations.matchAll(/\/\*\*([\s\S]*?)\*\/\s*runWorker/g)]
    .map((match) => match[1]);

  assert.equal(runWorkerDocs.length >= 2, true);
  for (const doc of runWorkerDocs) {
    assert.match(doc, /does not provide CPU thread offload/);
    assert.match(doc, /deferral/);
  }
  // No declaration or doc-comment may claim the closure runs on a worker
  // thread or off the main event loop.
  assert.doesNotMatch(declarations, /worker_threads/);
  assert.doesNotMatch(declarations, /runs? on a worker thread/i);
  assert.doesNotMatch(declarations, /off the main (?:event loop|thread)(?!\b.*not)/i);
});

function readTree(root) {
  const parts = [];
  visit(root);
  return parts.join('\n');

  function visit(current) {
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const fullPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        visit(fullPath);
      } else if (entry.isFile() && fullPath.endsWith('.d.ts')) {
        parts.push(fs.readFileSync(fullPath, 'utf8'));
      }
    }
  }
}
