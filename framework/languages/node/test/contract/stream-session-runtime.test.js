const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const connector = require('../../packages/stream-connector/dist');
const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const backend = require('../../packages/framework/dist/runtime/backend');
const nodeMonitorBackend = require('../../packages/framework/dist/runtime/backend/node/node-monitor-backend-adapter');

test('node monitor adapter preserves the opaque native session routing id', () => {
  let nativeHandler;
  let observed;
  const routingId = zlink.RoutingId.from(2);
  const monitor = nodeMonitorBackend.wrapMonitorSocket({
    close() {},
    recv() { return null; },
    onEvent(handler) { nativeHandler = handler; }
  });
  monitor.onEvent((event) => { observed = event; });

  nativeHandler({
    event: zlink.MonitorEventType.Disconnected,
    value: 0,
    routingId,
    localAddr: 'tcp://127.0.0.1:9000',
    remoteAddr: 'tcp://127.0.0.1:50000'
  });

  assert.equal(observed.routingId, routingId);
});

test('ConnectionReady before the first packet keeps the native routing id for replies', async () => {
  const socket = new FakeStreamSocket();
  const routingId = zlink.RoutingId.from(2);
  let monitorHandler;
  const bindingRuntime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: {
      createTextMessage(payload) { return zlink.Message.from(Buffer.from(payload)); },
      createBinaryMessage(payload) { return zlink.Message.from(Buffer.from(payload)); }
    }
  });
  const replied = new Promise((resolve) => {
    const runtime = new framework.ZLinkStreamSessionNodeRuntime({
      socket,
      bindingRuntime,
      monitor: { onEvent(handler) { monitorHandler = handler; } },
      headerDecoder: (header) => connector.ZlinkStreamHeaderCodec.decode(header.data()),
      sessionFactory(context) {
        return {
          context,
          async onDispatch() {
            context.client.reply('ready-first').submit();
            resolve(runtime);
          }
        };
      }
    });
    runtime.start();
    monitorHandler({
      nativeEvent: framework.ZLinkSocketNativeEventType.ConnectionReady,
      routingId,
      localAddr: 'tcp://local',
      remoteAddr: 'tcp://remote',
      value: 0
    });
    socket.emitPacket(routingId, fakeHeader({
      kind: connector.ZlinkStreamMessageKind.Request,
      requestSeq: 1n,
      name: 'ReadyFirst'
    }), fakeMessage('payload'));
  });

  const runtime = await replied;
  assert.equal(socket.sent.length, 1);
  assert.equal(socket.sent[0].routingId, routingId);
  await runtime.dispose();
});

test('actor session lifecycle serializes disconnect and replacement bind for the same actor', async () => {
  const lifecycle = new framework.ZLinkActorSessionLifecycleCoordinator();
  const events = [];
  let releaseDisconnect;
  const disconnectCanFinish = new Promise((resolve) => { releaseDisconnect = resolve; });
  let disconnectStarted;
  const disconnectDidStart = new Promise((resolve) => { disconnectStarted = resolve; });

  const disconnect = lifecycle.run('actor-1', async () => {
    events.push('disconnect:start');
    disconnectStarted();
    await disconnectCanFinish;
    events.push('disconnect:end');
  });
  await disconnectDidStart;
  const bind = lifecycle.run('actor-1', async () => {
    events.push('bind');
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, ['disconnect:start']);

  releaseDisconnect();
  await Promise.all([disconnect, bind]);
  assert.deepEqual(events, ['disconnect:start', 'disconnect:end', 'bind']);
});

test('stream session node runtime dispatches framed packets through one session context', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let dispatchCount = 0;
  let dispatchesDone;
  const dispatchesDonePromise = new Promise((resolve) => {
    dispatchesDone = resolve;
  });
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          events.push(['connected', ctx.sessionId, ctx.localAddr, ctx.remoteAddr]);
        },
        async onDispatch(header, payload) {
          events.push(['dispatch', header.packetName, payload.decode()]);
          dispatchCount += 1;
          if (dispatchCount === 2) {
            dispatchesDone();
          }
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-a', 'tcp://local', 'tcp://remote');
  socket.emitPacket('session-a', fakeHeader({ name: 'Ready' }), fakeMessage('one'));
  socket.emitPacket('session-a', fakeHeader({ name: 'Move' }), fakeMessage('two'));
  await dispatchesDonePromise;
  await runtime.dispose();

  assert.deepEqual(events, [
    ['connected', 'session-a', 'tcp://local', 'tcp://remote'],
    ['dispatch', 'Ready', 'one'],
    ['dispatch', 'Move', 'two'],
    ['disconnected', 'session-a']
  ]);
});

test('stream session node runtime serializes dispatch and disconnect callbacks per session', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let releaseFirst;
  const firstCanFinish = new Promise((resolve) => {
    releaseFirst = resolve;
  });
  let firstStarted;
  const firstStartedPromise = new Promise((resolve) => {
    firstStarted = resolve;
  });
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onDispatch(header, payload) {
          events.push(['dispatch:start', header.packetName, payload.decode()]);
          if (header.packetName === 'First') {
            firstStarted();
            await firstCanFinish;
          }
          events.push(['dispatch:end', header.packetName]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-serial', fakeHeader({ name: 'First' }), fakeMessage('one'));
  await firstStartedPromise;
  socket.emitPacket('session-serial', fakeHeader({ name: 'Second' }), fakeMessage('two'));
  runtime.markDisconnected('session-serial');
  assert.deepEqual(events, [['dispatch:start', 'First', 'one']]);

  releaseFirst();
  await runtime.dispose();

  assert.deepEqual(events, [
    ['dispatch:start', 'First', 'one'],
    ['dispatch:end', 'First'],
    ['dispatch:start', 'Second', 'two'],
    ['dispatch:end', 'Second'],
    ['disconnected', 'session-serial']
  ]);
});

test('stream session node runtime ignores unmatched monitor disconnect when multiple sessions exist', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: {
      onEvent(handler) {
        monitorHandler = handler;
      }
    },
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          events.push(['connected', ctx.sessionId]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-stale-a', 'tcp://local-a', 'tcp://remote-a');
  runtime.markConnected('session-stale-b', 'tcp://local-b', 'tcp://remote-b');
  await new Promise((resolve) => setImmediate(resolve));

  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: undefined,
    remoteAddr: undefined,
    routingId: undefined
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, [
    ['connected', 'session-stale-a'],
    ['connected', 'session-stale-b']
  ]);
  await runtime.dispose();
});

test('stream session node runtime uses monitor routing id to disconnect exactly one of multiple sessions', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: { onEvent(handler) { monitorHandler = handler; } },
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) { events.push(['connected', ctx.sessionId]); },
        async onDisconnected(ctx) { events.push(['disconnected', ctx.sessionId]); }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-a', 'tcp://local', 'tcp://remote-a');
  runtime.markConnected('session-b', 'tcp://local', 'tcp://remote-b');
  await new Promise((resolve) => setImmediate(resolve));
  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://remote-a',
    routingId: 'session-a'
  });
  await new Promise((resolve) => setImmediate(resolve));

  assert.deepEqual(events, [
    ['connected', 'session-a'],
    ['connected', 'session-b'],
    ['disconnected', 'session-a']
  ]);
  assert.equal(runtime.findSession('session-b').isDisconnected, false);
  await runtime.dispose();
});

test('stream session node runtime does not guess by endpoint when a disconnect lacks routing id and multiple sessions exist', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: { onEvent(handler) { monitorHandler = handler; } },
    sessionFactory(context) {
      return {
        context,
        async onDisconnected(ctx) { events.push(['disconnected', ctx.sessionId]); }
      };
    }
  });

  runtime.start();
  runtime.markConnected('old-session', 'tcp://local', 'tcp://old');
  runtime.markConnected('fresh-session', 'tcp://local', 'tcp://fresh');
  await new Promise((resolve) => setImmediate(resolve));
  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://fresh',
    routingId: undefined
  });
  await new Promise((resolve) => setImmediate(resolve));

  assert.deepEqual(events, []);
  await runtime.dispose();
});

test('stream session node runtime maps endpointless monitor disconnect to a single session', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: {
      onEvent(handler) {
        monitorHandler = handler;
      }
    },
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          events.push(['connected', ctx.sessionId]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-live', 'tcp://local-live', 'tcp://remote-live');
  await new Promise((resolve) => setImmediate(resolve));

  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: undefined,
    remoteAddr: undefined,
    routingId: undefined
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, [
    ['connected', 'session-live'],
    ['disconnected', 'session-live']
  ]);
  await runtime.dispose();
});

test('stream session node runtime cancels endpointless disconnect when connection-ready follows immediately', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: {
      onEvent(handler) {
        monitorHandler = handler;
      }
    },
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          events.push(['connected', ctx.sessionId]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-live');
  await new Promise((resolve) => setImmediate(resolve));

  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: undefined,
    remoteAddr: undefined,
    routingId: undefined
  });
  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.ConnectionReady,
    value: 0,
    localAddr: undefined,
    remoteAddr: undefined,
    routingId: undefined
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, [
    ['connected', 'session-live']
  ]);
  await runtime.dispose();
});

test('stream session node runtime cancels endpointless disconnect when another packet arrives before it is applied', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: {
      onEvent(handler) {
        monitorHandler = handler;
      }
    },
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          events.push(['connected', ctx.sessionId]);
        },
        async onDispatch(header, payload) {
          events.push(['dispatch', context.sessionId, header.packetName, payload.decode()]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-live');
  await new Promise((resolve) => setImmediate(resolve));

  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: undefined,
    remoteAddr: undefined,
    routingId: undefined
  });
  socket.emitPacket('session-next', fakeHeader({ name: 'Ready' }), fakeMessage('next'));
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, [
    ['connected', 'session-live'],
    ['dispatch', 'session-next', 'Ready', 'next']
  ]);
  await runtime.dispose();
});

test('stream session node runtime ignores monitor disconnect whose endpoint does not match the single session', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let monitorHandler;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    monitor: {
      onEvent(handler) {
        monitorHandler = handler;
      }
    },
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          events.push(['connected', ctx.sessionId]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-live', 'tcp://local-live', 'tcp://remote-live');
  await new Promise((resolve) => setImmediate(resolve));

  monitorHandler({
    nativeEvent: framework.ZLinkSocketNativeEventType.Disconnected,
    value: 0,
    localAddr: 'tcp://local-stale',
    remoteAddr: 'tcp://remote-stale',
    routingId: undefined
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, [
    ['connected', 'session-live']
  ]);
  await runtime.dispose();
});

test('stream session node runtime does not invoke user callbacks inside transport callback', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => ({ name: header.getString() }),
    sessionFactory(context) {
      return {
        context,
        async onDispatch(header, payload) {
          events.push(['dispatch', header.packetName, payload.decode()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-deferred', fakeHeader({ name: 'Deferred' }), fakeMessage('body'));
  assert.deepEqual(events, []);

  await runtime.dispose();
  assert.deepEqual(events, [['dispatch', 'Deferred', 'body']]);
});

test('stream session runtime rejects sessions that do not expose provided context', () => {
  const socket = new FakeStreamSocket();

  assert.throws(
    () => new framework.ZLinkStreamSessionRuntime({
      socket,
      sessionFactory() {
        return { context: {} };
      }
    }, 'session-b'),
    /provided by the stream runtime/
  );
});

test('stream session cleanup removes actor bindings without closing the stream again', async () => {
  const socket = new FakeStreamSocket();
  const bindingRuntime = new framework.ZLinkStreamBindingRuntime();
  const runtime = new framework.ZLinkStreamSessionRuntime({
    socket,
    bindingRuntime,
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          await ctx.actors.bind({ nodeRid: 'node-a', actorId: 'actor-a', generation: 1 });
        },
        async onDisconnected() {}
      };
    }
  }, 'session-c');

  runtime.enqueueConnected();
  await runtime.dispose();

  assert.equal(bindingRuntime.find('actor-a'), undefined);
  assert.equal(socket.disconnects.length, 0);
});

test('stream session onDisconnected can explicitly notify bound actors before cleanup', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  const bindingRuntime = new framework.ZLinkStreamBindingRuntime({
    async notifyDisconnected(actor) {
      events.push(['actor-disconnected', actor.actorId]);
    }
  });
  const runtime = new framework.ZLinkStreamSessionRuntime({
    socket,
    bindingRuntime,
    sessionFactory(context) {
      return {
        context,
        async onConnected(ctx) {
          await ctx.actors.bind({ nodeRid: 'node-a', actorId: 'actor-a', generation: 1 });
        },
        async onDisconnected(ctx) {
          await ctx.actors.bound[0].notifyDisconnected();
          events.push(['session-disconnected']);
        }
      };
    }
  }, 'session-disconnect');

  runtime.enqueueConnected();
  runtime.enqueueDisconnected();
  await runtime.dispose();

  assert.deepEqual(events, [
    ['actor-disconnected', 'actor-a'],
    ['session-disconnected']
  ]);
  assert.equal(bindingRuntime.find('actor-a'), undefined);
});

test('stream session node runtime closes rejected packets after dispose', async () => {
  const socket = new FakeStreamSocket();
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    sessionFactory(context) {
      return { context };
    }
  });
  const header = fakeMessage('h');
  const payload = fakeMessage('p');

  runtime.start();
  await runtime.dispose();
  socket.emitPacket('session-d', header, payload);

  assert.equal(header.closed, true);
  assert.equal(payload.closed, true);
});

test('stream session runtime replies to dispatch errors without session onError callback', async () => {
  const socket = new FakeStreamSocket();
  const errors = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    onError(error) {
      errors.push(['sink', error.message]);
    },
    sessionFactory(context) {
      return {
        context,
        async onDispatch() {
          throw new Error('dispatch failed');
        },
        async onError(_context, error) {
          errors.push(['session', error.error]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-e', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Request,
    requestSeq: 7n,
    name: 'Move'
  }), fakeMessage('p'));
  await runtime.dispose();

  assert.deepEqual(errors, [['sink', 'dispatch failed']]);
  assert.equal(socket.sent.length, 1);
  const frame = connector.ZlinkStreamFrameCodec.decode(socket.sent[0].payload.data());
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Error);
  assert.equal(header.requestSeq, 7n);
  assert.equal(header.name, 'Move');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), {
    code: 'Error',
    message: 'dispatch failed'
  });
});

test('stream session runtime keeps request streams open after route disconnect error replies', async () => {
  const socket = new FakeStreamSocket();
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    sessionFactory(context) {
      return {
        context,
        async onDispatch() {
          throw new framework.ZLinkRouteDisconnectedError('yield.spot.route', 512, 4);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-route-error', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Request,
    requestSeq: 9n,
    name: 'YieldShutdownScenarioReq'
  }), fakeMessage('p'));
  await runtime.dispose();

  assert.deepEqual(socket.disconnects, []);
  assert.equal(socket.sent.length, 1);
  const frame = connector.ZlinkStreamFrameCodec.decode(socket.sent[0].payload.data());
  const header = connector.ZlinkStreamHeaderCodec.decode(frame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Error);
  assert.equal(header.requestSeq, 9n);
  assert.equal(header.name, 'YieldShutdownScenarioReq');
  assert.deepEqual(JSON.parse(new TextDecoder().decode(frame.payload)), {
    code: 'ZLinkRouteDisconnectedError',
    message: 'yield.spot.route'
  });
});

test('stream session runtime completes pending responses before session dispatch', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  let pending;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    sessionFactory(context) {
      pending = context.startRequest(1000);
      return {
        context,
        async onDispatch(header, payload) {
          events.push(['dispatch', header.packetName, payload.decode()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-f', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Response,
    requestSeq: 1n,
    name: 'Move'
  }), fakeMessage('response-body'));

  const response = await pending.promise;
  await runtime.dispose();

  assert.equal(response.getString(), 'response-body');
  assert.deepEqual(events, []);
});

test('stream session runtime decompresses response frames before completing pending requests', async () => {
  const socket = new FakeStreamSocket();
  let pending;
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    sessionFactory(context) {
      pending = context.startRequest(1000);
      return { context };
    }
  });

  runtime.start();
  socket.emitPacket('session-compressed-response', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Response,
    flags: connector.ZlinkStreamHeaderFlags.PayloadCompressed,
    requestSeq: 1n,
    name: 'Move'
  }), fakeMessageBytes(Buffer.from('40551F41010047504141414141', 'hex')));

  const response = await pending.promise;
  await runtime.dispose();

  assert.equal(response.getString(), 'A'.repeat(96));
});

test('stream session runtime decompresses dispatch payloads before session handlers', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    sessionFactory(context) {
      return {
        context,
        async onDispatch(header, payload) {
          events.push(['dispatch', header.packetName, payload.decode()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-compressed-dispatch', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Send,
    flags: connector.ZlinkStreamHeaderFlags.PayloadCompressed,
    name: 'Move'
  }), fakeMessageBytes(Buffer.from('40551F41010047504141414141', 'hex')));
  await runtime.dispose();

  assert.deepEqual(events, [['dispatch', 'Move', 'A'.repeat(96)]]);
});

test('stream session runtime rejects compressed dispatch payloads above receive limit', async () => {
  const socket = new FakeStreamSocket();
  const errors = [];
  const events = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    onError(error) {
      errors.push(error);
    },
    sessionFactory(context) {
      return {
        context,
        async onDispatch(header, payload) {
          events.push(['dispatch', header.packetName, payload.decode()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-compressed-too-large', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Send,
    flags: connector.ZlinkStreamHeaderFlags.PayloadCompressed,
    name: 'Move'
  }), fakeMessageBytes(Uint8Array.from([0x40, 0x01, ...Buffer.from('A'.repeat(64 * 1024), 'utf8')])));
  await runtime.dispose();

  assert.deepEqual(events, []);
  assert.equal(errors.length, 1);
  assert.match(errors[0].message, /maximum stream payload size/);
});

test('stream session pending request timeout removes request sequence', async () => {
  const context = new framework.ZLinkStreamBindingRuntime().createSessionContext({
    sessionId: 'session-timeout',
    routingId: 'session-timeout',
    write() {
      return true;
    },
    async close() {}
  });
  const pending = context.startRequest(1);
  await assert.rejects(
    () => pending.promise,
    /Client stream request timed out/
  );

  const consumed = context.tryCompleteResponse({
    kind: 3,
    codec: 1,
    flags: 1,
    requestSeq: pending.requestSeq,
    name: 'LateReply',
    metadata: new Map()
  }, fakeMessage('late-body'));

  assert.equal(consumed, false);
});

test('stream session runtime dispatches unmatched response frames to the session', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => JSON.parse(header.getString(), streamHeaderReviver),
    sessionFactory(context) {
      return {
        context,
        async onDispatch(header, payload) {
          events.push(['dispatch', header.packetName, payload.decode()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-g', fakeHeader({
    kind: connector.ZlinkStreamMessageKind.Response,
    requestSeq: 99n,
    name: 'Move'
  }), fakeMessage('unmatched'));
  await runtime.dispose();

  assert.deepEqual(events, [['dispatch', 'Move', 'unmatched']]);
});

test('stream session node runtime receives framed packets from public binding stream socket', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const factory = new backend.ZLinkNodeBackendAdapterFactory();
  const context = factory.createChannelAdapter().createContext();
  const socket = factory.createStreamAdapter().createStreamSocket(context);
  let client;
  let runtime;
  const bindingRuntime = new framework.ZLinkStreamBindingRuntime({
    messageFactory: {
      createTextMessage(payload) {
        return zlink.Message.from(Buffer.from(payload));
      },
      createBinaryMessage(payload) {
        return zlink.Message.from(Buffer.from(payload));
      }
    }
  });

  const dispatched = new Promise((resolve) => {
    runtime = new framework.ZLinkStreamSessionNodeRuntime({
      socket,
      bindingRuntime,
      headerDecoder: (header) => connector.ZlinkStreamHeaderCodec.decode(header.data()),
      sessionFactory(sessionContext) {
        return {
          context: sessionContext,
          async onDispatch(header, payload) {
            resolve({
              sessionId: sessionContext.sessionId,
              routingId: sessionContext.routingId,
              header: header.packetName,
              payload: payload.decode()
            });
            sessionContext.client.reply('NativeReply').submit();
          }
        };
      }
    });
    runtime.start();
  });

  try {
    socket.bind(endpoint);
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');
    const response = once(client, 'data').then(([chunk]) => chunk);
    const requestHeader = connector.ZlinkStreamHeaderCodec.encode({
      kind: connector.ZlinkStreamMessageKind.Request,
      codec: connector.ZlinkStreamCodec.Json,
      flags: connector.ZlinkStreamHeaderFlags.HasRequestSeq,
      requestSeq: 7n,
      name: 'NativeHeader',
      metadata: connector.ZlinkStreamMetadataMap.empty
    });
    client.write(connector.ZlinkStreamFrameCodec.encode(
      requestHeader,
      new TextEncoder().encode('"NativePayload"')
    ));

    const received = await withTimeout(dispatched, 1000, 'native stream session dispatch');
    assert.equal(received.header, 'NativeHeader');
    assert.equal(received.payload, 'NativePayload');
    assert.equal(typeof received.sessionId, 'string');
    assert.equal(received.sessionId.length > 0, true);
    assert.equal(received.routingId, received.sessionId);
    const responseFrame = connector.ZlinkStreamFrameCodec.decode(
      await withTimeout(response, 1000, 'native stream session reply')
    );
    const responseHeader = connector.ZlinkStreamHeaderCodec.decode(responseFrame.header);
    assert.equal(responseHeader.kind, connector.ZlinkStreamMessageKind.Response);
    assert.equal(responseHeader.requestSeq, 7n);
    assert.equal(new TextDecoder().decode(responseFrame.payload), '"NativeReply"');
  } finally {
    await closeClient(client);
    await runtime?.dispose();
    await socket.dispose();
    await context.dispose();
  }
});

class FakeStreamSocket {
  constructor() {
    this.disconnects = [];
    this.sent = [];
    this.handler = undefined;
  }

  onFramedPacket(handler) {
    this.handler = handler;
  }

  send(routingId, payload, flags) {
    this.sent.push({ routingId, payload, flags });
    return true;
  }

  disconnectPeer(routingId) {
    this.disconnects.push(routingId);
  }

  emitPacket(routingId, header, payload) {
    this.handler(routingId, header, payload);
  }

  async dispose() {}
  async bindActor() {}
  async unbindActor() {}
  sendBoundActor() { return true; }
}

function fakeMessage(text) {
  const payload = Buffer.from(text);
  return {
    closed: false,
    data() {
      return payload;
    },
    toBytes() {
      return new Uint8Array(payload);
    },
    getString() {
      return text;
    },
    close() {
      this.closed = true;
    }
  };
}

function fakeMessageBytes(bytes) {
  const payload = new Uint8Array(bytes);
  return {
    closed: false,
    bytes: payload,
    toBytes() {
      return new Uint8Array(payload);
    },
    data() {
      return payload;
    },
    getString() {
      return new TextDecoder().decode(payload);
    },
    close() {
      this.closed = true;
    }
  };
}

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function closeClient(client) {
  if (client === undefined) {
    return;
  }
  if (client.destroyed) {
    return;
  }
  const closed = once(client, 'close');
  client.destroy();
  await closed;
}

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
  });
  return Promise.race([promise, guard]).finally(() => clearTimeout(timeout));
}

function fakeHeader(overrides) {
  return fakeMessageBytes(streamProtocol.encodeStreamHeader(streamHeader(overrides)));
}

function streamHeader(overrides) {
  return {
    kind: connector.ZlinkStreamMessageKind.Send,
    codec: connector.ZlinkStreamCodec.Json,
    flags: overrides.requestSeq === undefined
      ? connector.ZlinkStreamHeaderFlags.None
      : connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    name: 'Packet',
    metadata: new Map(),
    ...overrides
  };
}

function streamHeaderReviver(key, value) {
  if (key === 'requestSeq' && typeof value === 'string') {
    return BigInt(value);
  }
  if (key === 'metadata' && value?.values !== undefined) {
    return { values: new Map(value.values) };
  }
  return value;
}
