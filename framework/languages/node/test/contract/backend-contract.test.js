const assert = require('node:assert/strict');
const { once } = require('node:events');
const { spawn } = require('node:child_process');
const path = require('node:path');
const nodeTest = require('node:test');

const zlink = require('@zlink-systems/zlink');
const backend = require('../../packages/framework/dist/runtime/backend');
const framework = require('../../packages/framework/dist/internal');
const {
  ZLinkSubmitStatus
} = require('../../packages/framework/dist/runtime/messaging/submission-result');
const channelEnvelope = require('../../packages/framework/dist/runtime/channels/channel-envelope');

const removedSpotAdapterContracts = new Set([
  'backend adapter unwraps SpotNode when attaching stream SessionRelay',
  'backend stream bind converts public string actor node RID to native RoutingId',
  'backend adapter converts public string route bridge target RIDs to native RoutingId',
  'channel runtime connects route mesh peers before bind and spot route bridge attach',
  'backend adapter normalizes missing SpotNode actor lookup to undefined',
  'backend adapter submits actor destroy and closes actor bound sessions through public binding operations',
  'backend adapter aborts actor destroy waits and closes late reply messages',
  'backend adapter submits SpotNode actor bound-session send operation',
  'backend adapter submits SpotNode actor send through async completion callback',
  'backend adapter submits SpotNode actor request builder and wires callback'
]);

function test(name, body) {
  if (!removedSpotAdapterContracts.has(name)) {
    return nodeTest(name, body);
  }
  return nodeTest(`${name} is superseded by the formal MeshNode backend`, () => {
    const factory = new backend.ZLinkNodeBackendAdapterFactory();
    assert.equal(typeof factory.createMeshAdapter, 'function');
    assert.equal(factory.createSpotAdapter, undefined);
  });
}

test('backend adapter factory exposes the supported backend adapters', () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();

  assert.equal(typeof factory.createChannelAdapter, 'function');
  assert.equal(typeof factory.createMeshAdapter, 'function');
  assert.equal(factory.createSpotAdapter, undefined);
  assert.equal(typeof factory.createStreamAdapter, 'function');
  assert.equal(typeof factory.createMonitoringAdapter, 'function');
  assert.equal(factory.createRegistryAdapter, undefined);
});

test('backend mesh adapter creates a named MeshNode through the public binding API', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const context = factory.createChannelAdapter().createContext();
  const meshNode = factory.createMeshAdapter().createMeshNode(context, {
    meshName: 'backend.contract',
    routingId: `backend-node-${process.pid}`
  });

  try {
    meshNode.setBind(`inproc://backend-contract-${process.pid}`);
    meshNode.addChannelName('backend.channel');
    meshNode.start();
    assert.equal(meshNode.status().meshName, 'backend.contract');
    assert.equal(meshNode.status().channelCount, 1);
    assert.equal(meshNode.shutdown(1000), zlink.RequestResult.Ok);
  } finally {
    meshNode.close();
    await context.dispose();
  }
});

test('backend mesh dispatch pump drains a local channel record through claim and receive batches', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const context = factory.createChannelAdapter().createContext();
  const meshName = `backend.dispatch.${process.pid}`;
  const meshNode = factory.createMeshAdapter().createMeshNode(context, {
    meshName,
    routingId: `backend-dispatch-node-${process.pid}`
  });
  let pump;

  try {
    meshNode.setBind(`inproc://${meshName}`);
    meshNode.addChannelName('backend.dispatch');
    meshNode.start();
    const received = new Promise((resolve, reject) => {
      const timeout = setTimeout(
        () => reject(new Error('Mesh dispatch pump did not receive the local channel record.')),
        1000
      );
      pump = new backend.ZLinkMeshDispatchPump(meshNode, {
        dispatch(_owner, record) {
          clearTimeout(timeout);
          resolve({
            kind: record.kind,
            channelName: record.channelName,
            payload: record.parts[0].data().toString()
          });
        },
        reportError(error) {
          clearTimeout(timeout);
          reject(error);
        }
      });
      pump.start();
    });

    assert.equal(
      meshNode.sendToChannel('backend.dispatch', Buffer.from('mesh-pump')),
      zlink.SubmitResult.Ok
    );
    const record = await received;
    assert.equal(record.kind, zlink.ReceiveKind.ChannelSend);
    assert.equal(record.channelName, 'backend.dispatch');
    assert.equal(record.payload, 'mesh-pump');
  } finally {
    await pump?.dispose();
    meshNode.shutdown(1000);
    meshNode.close();
    await context.dispose();
  }
});

test('backend mesh dispatch pump yields between continuous receive batches', async () => {
  const totalBatches = 32;
  let readyHandler;
  let receivedBatches = 0;
  let timerObservedAt;
  let complete;
  const completed = new Promise((resolve) => { complete = resolve; });
  const claim = {
    recvBatch() {
      if (receivedBatches >= totalBatches) return { ok: false, records: [] };
      receivedBatches += 1;
      return { ok: true, records: [{ parts: [] }] };
    },
    release() {}
  };
  const node = {
    setReadyHandler(handler) { readyHandler = handler; },
    createReadyBatch() {
      return {
        reset() {},
        takeClaim() { return claim; },
        close() {}
      };
    },
    createReceiveBatch() {
      return {
        reset() {},
        close() {}
      };
    },
    drainReady() {
      return {
        ok: true,
        hasResidue: false,
        records: [{ ownerKind: zlink.ReadyOwnerKind.Node }]
      };
    }
  };
  const pump = new backend.ZLinkMeshDispatchPump(node, {
    dispatch() {
      if (receivedBatches === totalBatches) complete();
    }
  });

  try {
    pump.start();
    setTimeout(() => { timerObservedAt = receivedBatches; }, 0);
    readyHandler(zlink.ReadyDomain.Application);
    await completed;
    assert.ok(timerObservedAt < totalBatches, `timer ran after ${timerObservedAt} batches`);
  } finally {
    await pump.dispose();
  }
});

test('backend mesh dispatch pump yields between continuous ready batches without messages', async () => {
  const totalBatches = 32;
  let readyHandler;
  let drainedBatches = 0;
  let timerObservedAt;
  let complete;
  const completed = new Promise((resolve) => { complete = resolve; });
  const node = {
    setReadyHandler(handler) { readyHandler = handler; },
    createReadyBatch() {
      return {
        reset() {},
        takeClaim() {
          return {
            recvBatch() { return { ok: false, records: [] }; },
            release() {}
          };
        },
        close() {}
      };
    },
    createReceiveBatch() {
      return {
        reset() {},
        close() {}
      };
    },
    drainReady() {
      drainedBatches += 1;
      if (drainedBatches === totalBatches) complete();
      return {
        ok: true,
        hasResidue: drainedBatches < totalBatches,
        records: [{ ownerKind: zlink.ReadyOwnerKind.Node }]
      };
    }
  };
  const pump = new backend.ZLinkMeshDispatchPump(node, { dispatch() {} });

  try {
    pump.start();
    setTimeout(() => { timerObservedAt = drainedBatches; }, 0);
    readyHandler(zlink.ReadyDomain.Application);
    await completed;
    assert.ok(timerObservedAt < totalBatches, `timer ran after ${timerObservedAt} ready batches`);
  } finally {
    await pump.dispose();
  }
});

test('backend mesh dispatch pump yields before recursively signaled dispatch work', async () => {
  const totalDispatches = 64;
  let readyHandler;
  let dispatches = 0;
  let timerObservedAt;
  let complete;
  const completed = new Promise((resolve) => { complete = resolve; });
  const node = {
    setReadyHandler(handler) { readyHandler = handler; },
    createReadyBatch() {
      return {
        reset() {},
        takeClaim() {
          let received = false;
          return {
            recvBatch() {
              if (received) return { ok: false, records: [] };
              received = true;
              return { ok: true, records: [{ parts: [] }] };
            },
            release() {}
          };
        },
        close() {}
      };
    },
    createReceiveBatch() {
      return {
        reset() {},
        close() {}
      };
    },
    drainReady() {
      return {
        ok: true,
        hasResidue: false,
        records: [{ ownerKind: zlink.ReadyOwnerKind.Node }]
      };
    }
  };
  const pump = new backend.ZLinkMeshDispatchPump(node, {
    dispatch() {
      dispatches += 1;
      if (dispatches === totalDispatches) {
        complete();
      } else {
        readyHandler(zlink.ReadyDomain.Application);
      }
    }
  });

  try {
    pump.start();
    setTimeout(() => { timerObservedAt = dispatches; }, 0);
    readyHandler(zlink.ReadyDomain.Application);
    await completed;
    assert.ok(timerObservedAt < totalDispatches, `timer ran after ${timerObservedAt} recursive dispatches`);
  } finally {
    await pump.dispose();
  }
});

test('backend mesh dispatch pump drains ready work queued before an async handler waits', async () => {
  let readyHandler;
  let drainedBatches = 0;
  let resolveFirstDispatch;
  const firstDispatchReleased = new Promise((resolve) => { resolveFirstDispatch = resolve; });
  let resolveSecondDispatch;
  const secondDispatched = new Promise((resolve) => { resolveSecondDispatch = resolve; });
  const node = {
    setReadyHandler(handler) { readyHandler = handler; },
    createReadyBatch() {
      return {
        reset() {},
        takeClaim() {
          let received = false;
          const batch = drainedBatches;
          return {
            recvBatch() {
              if (received) return { ok: false, records: [] };
              received = true;
              return { ok: true, records: [{ parts: [], batch }] };
            },
            release() {}
          };
        },
        close() {}
      };
    },
    createReceiveBatch() {
      return {
        reset() {},
        close() {}
      };
    },
    drainReady() {
      drainedBatches += 1;
      if (drainedBatches === 1) {
        readyHandler(zlink.ReadyDomain.Application);
      }
      return {
        ok: true,
        hasResidue: false,
        records: [{ ownerKind: zlink.ReadyOwnerKind.Node }]
      };
    }
  };
  const pump = new backend.ZLinkMeshDispatchPump(node, {
    async dispatch(_owner, record) {
      if (record.batch === 1) {
        await firstDispatchReleased;
        return;
      }
      resolveSecondDispatch();
      resolveFirstDispatch();
    }
  });

  try {
    pump.start();
    readyHandler(zlink.ReadyDomain.Application);
    await Promise.race([
      secondDispatched,
      new Promise((_, reject) => setTimeout(
        () => reject(new Error('Queued MeshNode ready work was not dispatched.')),
        1000
      ))
    ]);
    assert.equal(drainedBatches, 2);
  } finally {
    resolveFirstDispatch();
    await pump.dispose();
  }
});

test('backend mesh record dispatcher routes node, spot, actor, and infrastructure records', async () => {
  const routed = [];
  const dispatcher = new backend.ZLinkMeshRecordDispatcher({
    node: (record) => routed.push(['node', record.kind]),
    spot: (owner, record) => routed.push(['spot', owner.ownerKind, record.kind]),
    actor: (owner, record) => routed.push(['actor', owner.ownerKind, record.kind]),
    completion: (record) => routed.push(['completion', record.kind]),
    sendReady: (record) => routed.push(['sendReady', record.kind]),
    transferControl: (record) => routed.push(['transferControl', record.kind])
  });
  const owner = (ownerKind) => ({ ownerKind });
  const record = (kind) => ({ kind });

  await dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Node), record(zlink.ReceiveKind.ChannelRequest));
  await dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Spot), record(zlink.ReceiveKind.SpotControl));
  await dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Actor), record(zlink.ReceiveKind.ActorSend));
  await dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Node), record(zlink.ReceiveKind.Completion));
  await dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Node), record(zlink.ReceiveKind.SendReady));
  await dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Node), record(zlink.ReceiveKind.TransferControl));

  assert.deepEqual(routed, [
    ['node', zlink.ReceiveKind.ChannelRequest],
    ['spot', zlink.ReadyOwnerKind.Spot, zlink.ReceiveKind.SpotControl],
    ['actor', zlink.ReadyOwnerKind.Actor, zlink.ReceiveKind.ActorSend],
    ['completion', zlink.ReceiveKind.Completion],
    ['sendReady', zlink.ReceiveKind.SendReady],
    ['transferControl', zlink.ReceiveKind.TransferControl]
  ]);
  assert.throws(
    () => dispatcher.dispatch(owner(zlink.ReadyOwnerKind.Spot), record(zlink.ReceiveKind.ChannelSend)),
    /requires owner kind/
  );
});

test('MeshNode runtime manager owns lifecycle and forwards pull-dispatch records', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const context = factory.createChannelAdapter().createContext();
  const meshName = `runtime.dispatch.${process.pid}`;
  const registration = framework.createFrameworkRegistrationWithBuilder((builder) => {
    const mesh = builder.addRouteMesh(meshName)
      .listen(`inproc://${meshName}`)
      .routingId(`runtime-node-${process.pid}`);
    mesh.channelName(meshName);
  });
  let resolveRecord;
  const received = new Promise((resolve) => {
    resolveRecord = resolve;
  });
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration,
    backendAdapterFactory: factory,
    context,
    meshRecordDispatcher(_name, _owner, record) {
      resolveRecord({
        kind: record.kind,
        channelName: record.channelName,
        payload: record.parts[0].data().toString()
      });
    }
  });

  try {
    await runtime.start();
    assert.equal(runtime.meshNodesByName.size, 1);
    assert.equal(
      runtime.primaryMeshNode.sendToChannel(meshName, Buffer.from('runtime-pump')),
      zlink.SubmitResult.Ok
    );
    let timeout;
    const record = await Promise.race([
      received.finally(() => clearTimeout(timeout)),
      new Promise((_, reject) => {
        timeout = setTimeout(
          () => reject(new Error('Runtime MeshNode dispatch timed out.')),
          1000
        );
      })
    ]);
    assert.equal(record.kind, zlink.ReceiveKind.ChannelSend);
    assert.equal(record.channelName, meshName);
    assert.equal(record.payload, 'runtime-pump');
  } finally {
    await runtime.dispose();
    await context.dispose();
  }
});

test('Logical Multicast completion does not expose or record target admission results', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let coreCalls = 0;
  runtime.publishers.set('play', {
    async publishAsync() {
      coreCalls += 1;
    }
  });

  const result = await runtime.publish('play', 'events', 'score', 'ProfileChanged', { sequence: 1 });

  assert.equal(coreCalls, 1);
  assert.deepEqual(result, { status: ZLinkSubmitStatus.Submitted });
});

test('Logical Multicast terminal completes while target processing remains blocked', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let coreCalls = 0;
  let completeTarget;
  runtime.publishers.set('play', {
    publishAsync() {
      coreCalls += 1;
      return new Promise((resolve) => { completeTarget = resolve; });
    }
  });

  const result = await runtime.publish(
    'play', 'events', 'score', 'ProfileChanged', { sequence: 1 }
  );
  assert.equal(coreCalls, 1);
  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
  assert.equal(runtime.activePublishes.has('play'), false);
  completeTarget();
});

test('Logical Multicast post-start failure does not change the caller terminal', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let failTarget;
  runtime.publishers.set('play', {
    publishAsync() {
      return new Promise((_resolve, reject) => { failTarget = reject; });
    }
  });

  const result = await runtime.publish(
    'play', 'events', 'score', 'ProfileChanged', { sequence: 1 }
  );
  failTarget(new Error('target failed after handoff'));
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
});

test('Logical Multicast shutdown after handoff does not change the caller terminal', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let coreCalls = 0;
  runtime.publishers.set('play', {
    publishAsync() {
      coreCalls += 1;
      return new Promise(() => undefined);
    },
    async close() {}
  });

  const submitted = await runtime.publish(
    'play', 'events', 'score', 'ProfileChanged', { sequence: 1 }
  );
  await runtime.dispose();
  assert.equal(submitted.status, ZLinkSubmitStatus.Submitted);
  await assert.rejects(
    runtime.publish('play', 'events', 'score', 'ProfileChanged', { sequence: 2 }),
    (error) => {
      assert.equal(error instanceof framework.ZLinkFrameworkException, true);
      assert.equal(error.kind, framework.ZLinkFrameworkErrorKind.RuntimeShutdown);
      return true;
    }
  );
  assert.equal(coreCalls, 1);
  assert.equal(runtime.activePublishes.has('play'), false);
});

test('Logical Multicast accepts zero subscribers as normal completion', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  runtime.publishers.set('play', {
    async publishAsync() {
    }
  });

  const result = await runtime.publish(
    'play', 'events', 'score', 'ProfileChanged', { sequence: 1 }
  );

  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
});

test('Logical Multicast releases its handoff slot after envelope encoding fails', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let coreCalls = 0;
  runtime.publishers.set('play', {
    async publishAsync() {
      coreCalls += 1;
    }
  });
  const cyclic = {};
  cyclic.self = cyclic;

  await assert.rejects(
    runtime.publish('play', 'events', 'score', 'ProfileChanged', cyclic),
    /circular/i
  );
  assert.equal(coreCalls, 0);

  const result = await runtime.publish(
    'play', 'events', 'score', 'ProfileChanged', { sequence: 2 }
  );
  assert.equal(coreCalls, 1);
  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
});

test('Logical Multicast abort before handoff calls Core zero times', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let coreCalls = 0;
  runtime.publishers.set('play', {
    async publishAsync() {
      coreCalls += 1;
      throw new Error('must not run');
    }
  });
  const controller = new AbortController();
  const reason = new Error('cancel before commit');
  controller.abort(reason);

  await assert.rejects(
    runtime.publish('play', 'events', 'score', 'ProfileChanged', { sequence: 1 }, controller.signal),
    (error) => error === reason
  );
  assert.equal(coreCalls, 0);
});

test('Logical Multicast abort after Core start preserves committed completion', async () => {
  const runtime = new framework.ZLinkSpotNodeRuntimeManager({
    registration: framework.createFrameworkRegistration({}),
    backendAdapterFactory: {},
    context: {}
  });
  let coreCalls = 0;
  let complete;
  let forwardedSignal;
  runtime.publishers.set('play', {
    publishAsync(channelName, topic, parts, options, signal) {
      coreCalls += 1;
      forwardedSignal = signal;
      return new Promise((resolve) => { complete = resolve; });
    }
  });
  const controller = new AbortController();
  const pending = runtime.publish(
    'play', 'events', 'score', 'ProfileChanged', { sequence: 1 }, controller.signal
  );
  assert.equal(forwardedSignal, undefined);
  controller.abort(new Error('late abort'));

  const result = await pending;
  assert.equal(coreCalls, 1);
  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
  complete();
});

test('Logical Multicast binding commit preserves admission after post-start abort', async () => {
  const fixture = path.join(__dirname, 'fixtures', 'logical-multicast-commit-child.js');
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
  assert.match(stdout, /logical-multicast-commit-ok/);
});

test('framework host dispatches a MeshNode channel record through registered handler lifecycle', async () => {
  const meshName = `host.dispatch.${process.pid}`;
  let resolveHandled;
  const handled = new Promise((resolve) => {
    resolveHandled = resolve;
  });
  class MeshNotice {
    handle(message, context) {
      resolveHandled({ message, context });
    }
  }
  const registration = framework.createFrameworkRegistrationWithBuilder((builder) => {
    const mesh = builder.addRouteMesh(meshName)
      .listen(`inproc://${meshName}`)
      .routingId(`host-dispatch-node-${process.pid}`);
    mesh.channelName(meshName).addSendHandler(MeshNotice);
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });

  try {
    await host.start();
    const node = host.requirePrimaryMeshNode();
    assert.equal(
      node.sendToChannel(
        meshName,
        channelEnvelope.encodeChannelEnvelopeParts(
          3,
          meshName,
          'MeshNotice',
          { value: 'handled' }
        )
      ),
      zlink.SubmitResult.Ok
    );
    let timeout;
    const result = await Promise.race([
      handled.finally(() => clearTimeout(timeout)),
      new Promise((_, reject) => {
        timeout = setTimeout(() => reject(new Error('Mesh channel handler dispatch timed out.')), 1000);
      })
    ]);
    assert.deepEqual(result.message, { value: 'handled' });
    assert.equal(result.context.channelName, meshName);
    assert.equal(result.context.packetName, 'MeshNotice');
  } finally {
    await host.stop();
  }
});

test('framework host dispatches MeshNode node-direct send and request records', async () => {
  const meshName = `host.node-direct.${process.pid}`;
  let resolveHandled;
  const handled = new Promise((resolve) => {
    resolveHandled = resolve;
  });
  class DirectNotice {
    handle(message, context) {
      resolveHandled({ message, context });
    }
  }
  class DirectQuery {
    handle(message, context) {
      return { echoed: message.value, source: context.sourceNodeRid };
    }
  }
  const registration = framework.createFrameworkRegistrationWithBuilder((builder) => {
    const mesh = builder.addRouteMesh(meshName)
      .listen(`inproc://${meshName}`)
      .routingId(`host-node-direct-${process.pid}`)
      .addRouteSendHandler(DirectNotice)
      .addRouteRequestHandler(DirectQuery);
    mesh.channelName(`${meshName}.direct`);
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });

  try {
    await host.start();
    const node = host.requirePrimaryMeshNode();
    const target = node.status().routingId;
    const sendParts = channelEnvelope.encodeChannelEnvelopeParts(
      3,
      meshName,
      'DirectNotice',
      { value: 'sent' }
    ).map((part) => zlink.Message.from(part));
    await host.dispatchMeshRecord(meshName, { ownerKind: zlink.ReadyOwnerKind.Node }, {
      kind: zlink.ReceiveKind.NodeSend,
      sourceNodeRid: target,
      parts: sendParts
    });
    const sent = await handled;
    assert.deepEqual(sent.message, { value: 'sent' });
    assert.equal(sent.context.routerChannelId, meshName);
    assert.equal(String(sent.context.sourceNodeRid), String(target));

    const requestParts = channelEnvelope.encodeChannelEnvelopeParts(
      1,
      meshName,
      'DirectQuery',
      { value: 'asked' }
    ).map((part) => zlink.Message.from(part));
    let replyParts;
    await host.dispatchMeshRecord(meshName, { ownerKind: zlink.ReadyOwnerKind.Node }, {
      kind: zlink.ReceiveKind.NodeRequest,
      sourceNodeRid: target,
      parts: requestParts,
      reply(parts) {
        replyParts = parts.map((part) => zlink.Message.from(part));
        return zlink.SubmitResult.Ok;
      }
    });
    try {
      const reply = channelEnvelope.decodeChannelEnvelope(replyParts);
      assert.deepEqual(JSON.parse(reply.payload.toString()), {
        echoed: 'asked',
        source: String(target)
      });
    } finally {
      for (const part of sendParts) part.close();
      for (const part of requestParts) part.close();
      for (const part of replyParts) part.close();
    }
  } finally {
    await host.stop();
  }
});

test('backend adapter creates context and core socket wrappers through public binding API', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const context = channel.createContext();
  const disposables = [];

  try {
    const dealer = channel.createDealerSocket(context);
    const router = channel.createRouterSocket(context);
    const publisher = channel.createPublisherSocket(context);
    const subscriber = channel.createSubscriberSocket(context);
    const topicMessage = channel.createTopicMessage();
    const subscriberPoller = channel.createReadablePoller(subscriber);
    const stream = factory.createStreamAdapter().createStreamSocket(context);

    disposables.push(dealer, router, publisher, subscriber, subscriberPoller, stream);

    assert.equal(Array.isArray(topicMessage.parts), true);
    assert.equal(typeof dealer.dispose, 'function');
    assert.equal(typeof router.dispose, 'function');
    assert.equal(typeof publisher.dispose, 'function');
    assert.equal(typeof subscriber.dispose, 'function');
    assert.equal(typeof subscriberPoller.wait, 'function');
    assert.equal(typeof subscriberPoller.dispose, 'function');
    assert.equal(typeof stream.dispose, 'function');
  } finally {
    for (const disposable of disposables.reverse()) {
      await disposable.dispose();
    }
    await context.dispose();
  }
});

test('backend adapter unwraps SpotNode when attaching stream SessionRelay', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const streamAdapter = factory.createStreamAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const stream = streamAdapter.createStreamSocket(context);

  try {
  } finally {
    await stream.dispose();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend stream bind converts public string actor node RID to native RoutingId', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const streamAdapter = factory.createStreamAdapter();
  const context = channel.createContext();
  const sessionNode = spotAdapter.createSpotNode(context, 3);
  const playNode = spotAdapter.createSpotNode(context, 3);
  const stream = streamAdapter.createStreamSocket(context);

  try {
    sessionNode.setRoutingId('backend-session-node');
    playNode.setRoutingId('backend-play-node');
    const actorRef = playNode.createActor('backend-player');

    await stream.bindActor(
      'backend-session',
      {
        nodeRid: String(actorRef.nodeRid),
        actorId: actorRef.actorId,
        generation: actorRef.generation
      },
      1000
    );
  } finally {
    await stream.dispose();
    await playNode.dispose();
    await sessionNode.dispose();
    await context.dispose();
  }
});

test('backend adapter converts public string route bridge target RIDs to native RoutingId', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const router = channel.createRouterSocket(context);
  const bridge = spotNode.createRouteBridge();

  try {
    router.setRoutingId('backend-bridge-source');
    bridge.attachRouterChannel('mesh', router, { capabilities: 3 });

    assert.equal(typeof bridge.send('mesh', 'backend-bridge-target', 'backend-bridge-spot').message, 'function');
    assert.equal(typeof bridge.request('mesh', 'backend-bridge-target', 'backend-bridge-spot').message, 'function');
  } finally {
    await bridge.dispose();
    await router.dispose();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('channel runtime connects route mesh peers before bind and spot route bridge attach', () => {
  const calls = [];
  const registration = framework.createFrameworkRegistrationWithBuilder((builder) => {
    builder.addRouteMesh('room.route')
      .enableServer('tcp://0.0.0.0:9410')
      .enableClient('tcp://127.0.0.1:9410');
    builder.addRouteMesh('room')
      .listen('tcp://0.0.0.0:9411', 'room-node');
  });
  const routeSocket = {
    nativeInstance: {},
    setChannelName(channelName) { calls.push(`route:setChannelName:${channelName}`); },
    setRoutingId(routingId) { calls.push(`route:setRoutingId:${routingId}`); },
    connect(endpoint) { calls.push(`route:connect:${endpoint}`); },
    bind(endpoint) { calls.push(`route:bind:${endpoint}`); },
    disconnect() {},
    onSendReady() {},
    recv() { return undefined; },
    async dispose() {}
  };
  const spotNode = {
    routingId: 'room-node',
    createRouteBridge() {
      calls.push('spot:createRouteBridge');
      return {
        attachRouterChannel(channelName) { calls.push(`bridge:attachRouter:${channelName}`); },
        async dispose() {}
      };
    }
  };
  const runtime = new framework.ZLinkChannelRuntimeManager(
    registration,
    {
      createRouterSocket() {
        calls.push('route:createRouter');
        return routeSocket;
      }
    },
    { nativeInstance: {}, shutdown() {}, async dispose() {} }
  );

  runtime.bindRouteMeshRouters();
  runtime.setSpotNodes(new Map([['room', spotNode]]));
  runtime.start({
    errorSink: { report() {} },
    run(_name, _task) {
      return Promise.resolve();
    }
  });

  assert.deepEqual(calls, [
    'route:createRouter',
    'route:setChannelName:room.route',
    'route:connect:tcp://127.0.0.1:9410',
    'route:bind:tcp://0.0.0.0:9410',
    'spot:createRouteBridge',
    'bridge:attachRouter:room.route'
  ]);
});

test('backend adapter normalizes missing SpotNode actor lookup to undefined', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);

  try {
    assert.equal(spotNode.actorLookup('missing-actor'), undefined);

    const actorRef = spotNode.createActor('existing-actor');
    assert.equal(actorRef.actorId, 'existing-actor');
    assert.equal(typeof actorRef.generation, 'bigint');
    assert.deepEqual(spotNode.actorLookup('existing-actor'), actorRef);
  } finally {
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits actor destroy and closes actor bound sessions through public binding operations', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const calls = [];
  let replyClosed = false;

  try {
    spotNode.nativeInstance.createActor = (actorId) => ({
      actorRef: { nodeRid: zlink.RoutingId.from('backend-node'), actorId, generation: 1n },
      closeBoundSession(timeoutMs) {
        calls.push({ kind: 'closeBoundSession', timeoutMs });
      }
    });
    spotNode.nativeInstance.destroyActor = (actorRef) => ({
      timeout(timeoutMs) {
        calls.push({ kind: 'destroyTimeout', actorRef, timeoutMs });
        return this;
      },
      submit() {
        calls.push({ kind: 'destroySubmit' });
        return Promise.resolve([{ close() { replyClosed = true; } }]);
      }
    });

    const actorRef = spotNode.createActor('backend-destroy');
    await spotNode.closeActorBoundSession(actorRef, 17);
    await spotNode.destroyActor(actorRef, 23);

    assert.deepEqual(calls.map((call) => call.kind), [
      'closeBoundSession',
      'destroyTimeout',
      'destroySubmit'
    ]);
    assert.equal(calls[0].timeoutMs, 17);
    assert.equal(calls[1].timeoutMs, 23);
    assert.equal(calls[1].actorRef.actorId, 'backend-destroy');
    assert.equal(replyClosed, true);
    await assert.rejects(
      spotNode.closeActorBoundSession(actorRef, 0),
      (error) => error instanceof zlink.ConfigError && error.result === zlink.ConfigResult.NotFound
    );
  } finally {
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter aborts actor destroy waits and closes late reply messages', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const abort = new AbortController();
  let completeDestroy;
  let replyClosed = false;

  try {
    spotNode.nativeInstance.createActor = (actorId) => ({
      actorRef: { nodeRid: zlink.RoutingId.from('backend-node'), actorId, generation: 1n },
      closeBoundSession() {}
    });
    spotNode.nativeInstance.destroyActor = () => ({
      timeout() { return this; },
      submit() {
        return new Promise((resolve) => {
          completeDestroy = resolve;
        });
      }
    });

    const actorRef = spotNode.createActor('backend-abort-destroy');
    const destroy = spotNode.destroyActor(actorRef, 1000, abort.signal);
    const reason = new Error('cancel backend actor destroy');
    abort.abort(reason);
    await assert.rejects(destroy, reason);

    completeDestroy([{ close() { replyClosed = true; } }]);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(replyClosed, true);
    await assert.rejects(
      spotNode.closeActorBoundSession(actorRef, 0),
      (error) => error instanceof zlink.ConfigError && error.result === zlink.ConfigResult.NotFound
    );
  } finally {
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits SpotNode actor bound-session send operation', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const frame = zlink.Message.from(Buffer.from('frame'));

  try {
    const actorRef = spotNode.createActor('unbound-actor');
    assert.throws(
      () => spotNode.sendActorBoundSession(actorRef, [frame], 1),
      /actor bound session send failed|not found|No such file|ENOENT|NOT_FOUND/i
    );
  } finally {
    frame.close();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits SpotNode actor send through async completion callback', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const frame = zlink.Message.from(Buffer.from('actor-send'));
  const actorRef = { nodeRid: 'backend-actor-node', actorId: 'backend-actor', generation: 1n };
  const calls = [];

  try {
    spotNode.nativeInstance.sendToActorCallback = (actor, parts, callback, flags, timeoutMs) => {
      calls.push({ actor, parts, flags, timeoutMs });
      queueMicrotask(() => callback(zlink.RequestResult.Ok, []));
      return true;
    };

    assert.equal(await spotNode.sendToActor(actorRef, [frame], zlink.SendFlags.None), true);
    assert.equal(calls.length, 1);
    assert.equal(calls[0].actor.actorId, 'backend-actor');
    assert.equal(calls[0].parts.length, 1);
  } finally {
    frame.close();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend adapter submits SpotNode actor request builder and wires callback', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const spotAdapter = factory.createSpotAdapter();
  const context = channel.createContext();
  const spotNode = spotAdapter.createSpotNode(context, 3);
  const frame = zlink.Message.from(Buffer.from('actor-request'));
  const actorRef = { nodeRid: 'backend-actor-node', actorId: 'backend-actor', generation: 1n };
  const calls = [];

  try {
    spotNode.nativeInstance.requestToActor = (actor) => ({
      message(part) {
        calls.push({ kind: 'message', actor, part });
        return this;
      },
      flags(flags) {
        calls.push({ kind: 'flags', flags });
        return this;
      },
      timeout(timeoutMs) {
        calls.push({ kind: 'timeout', timeoutMs });
        return this;
      },
      submit(callback) {
        calls.push({ kind: 'submit' });
        callback(zlink.RequestResult.Ok, []);
        return true;
      }
    });

    let completed = false;
    const accepted = spotNode.requestToActor(
      actorRef,
      [frame],
      (result, parts) => {
        completed = true;
        assert.equal(result, zlink.RequestResult.Ok);
        assert.deepEqual(parts, []);
      },
      zlink.SendFlags.None,
      123
    );

    assert.equal(accepted, true);
    assert.equal(completed, true);
    assert.deepEqual(calls.map((call) => call.kind), ['message', 'timeout', 'submit']);
  } finally {
    frame.close();
    await spotNode.dispose();
    await context.dispose();
  }
});

test('backend router recv normalizes transient route recv invalid handle to no message', async () => {
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const channel = factory.createChannelAdapter();
  const context = channel.createContext();
  const router = channel.createRouterSocket(context);

  try {
    await router.dispose();
    assert.equal(router.recv(1), undefined);
  } finally {
    await context.dispose();
  }
});

test('backend socket wrapper treats missing route disconnect as idempotent cleanup', () => {
  const error = new zlink.ConfigError(zlink.ConfigResult.NotFound, 2);

  assert.equal(backend.isDisconnectRouteNotFoundError(error), true);
});

test('subscriber receive loop never blocks the Node event loop while polling', async () => {
  const waits = [];
  const loop = new framework.ZLinkSubscriberReceiveLoop(
    {
      createReadablePoller() {
        return {
          wait(timeoutMs) {
            waits.push(timeoutMs);
            return false;
          },
          dispose() {}
        };
      }
    },
    {},
    { async dispatch() {} }
  );

  const running = loop.run();
  while (waits.length === 0) await new Promise((resolve) => setImmediate(resolve));
  await loop.stop();
  await running;

  assert.ok(waits.length > 0);
  assert.deepEqual([...new Set(waits)], [0]);
});
