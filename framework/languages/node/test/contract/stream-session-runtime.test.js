const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const test = require('node:test');

const connector = require('../../packages/stream-connector/dist');
const framework = require('../../packages/framework/dist');

test('stream session node runtime dispatches framed packets through one session context', async () => {
  const socket = new FakeStreamSocket();
  const events = [];
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
          events.push(['dispatch', header.name, payload.getString()]);
        },
        async onDisconnected(ctx) {
          events.push(['disconnected', ctx.sessionId]);
        }
      };
    }
  });

  runtime.start();
  runtime.markConnected('session-a', 'tcp://local', 'tcp://remote');
  socket.emitPacket('session-a', fakeMessage('Ready'), fakeMessage('one'));
  socket.emitPacket('session-a', fakeMessage('Move'), fakeMessage('two'));
  await runtime.dispose();

  assert.deepEqual(events, [
    ['connected', 'session-a', 'tcp://local', 'tcp://remote'],
    ['dispatch', 'Ready', 'one'],
    ['dispatch', 'Move', 'two'],
    ['disconnected', 'session-a']
  ]);
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

test('stream session runtime reports dispatch errors through session onError and runtime sink', async () => {
  const socket = new FakeStreamSocket();
  const errors = [];
  const runtime = new framework.ZLinkStreamSessionNodeRuntime({
    socket,
    headerDecoder: (header) => header.getString(),
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
          errors.push(['session', error.diagnostic.message]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-e', fakeMessage('h'), fakeMessage('p'));
  await runtime.dispose();

  assert.deepEqual(errors, [
    ['sink', 'dispatch failed'],
    ['session', 'dispatch failed']
  ]);
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
          events.push(['dispatch', header.name, payload.getString()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-f', fakeMessage(JSON.stringify(streamHeaderJson({
    kind: connector.ZlinkStreamMessageKind.Response,
    requestSeq: 1n,
    name: 'Move'
  }))), fakeMessage('response-body'));

  const response = await pending.promise;
  await runtime.dispose();

  assert.equal(response.getString(), 'response-body');
  assert.deepEqual(events, []);
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
          events.push(['dispatch', header.name, payload.getString()]);
        }
      };
    }
  });

  runtime.start();
  socket.emitPacket('session-g', fakeMessage(JSON.stringify(streamHeaderJson({
    kind: connector.ZlinkStreamMessageKind.Response,
    requestSeq: 99n,
    name: 'Move'
  }))), fakeMessage('unmatched'));
  await runtime.dispose();

  assert.deepEqual(events, [['dispatch', 'Move', 'unmatched']]);
});

test('stream session node runtime receives framed packets from public binding stream socket', async () => {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const factory = new framework.ZLinkNodeBackendAdapterFactory();
  const context = factory.createChannelAdapter().createContext();
  const socket = factory.createStreamAdapter().createStreamSocket(context);
  let client;
  let runtime;

  const dispatched = new Promise((resolve) => {
    runtime = new framework.ZLinkStreamSessionNodeRuntime({
      socket,
      headerDecoder: (header) => header.getString(),
      sessionFactory(sessionContext) {
        return {
          context: sessionContext,
          async onDispatch(header, payload) {
            resolve({
              sessionId: sessionContext.sessionId,
              routingId: sessionContext.routingId,
              header,
              payload: payload.getString()
            });
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
    client.write(connector.ZlinkStreamFrameCodec.encode(
      new TextEncoder().encode('NativeHeader'),
      new TextEncoder().encode('NativePayload')
    ));

    const received = await withTimeout(dispatched, 1000, 'native stream session dispatch');
    assert.equal(received.header, 'NativeHeader');
    assert.equal(received.payload, 'NativePayload');
    assert.equal(typeof received.sessionId, 'string');
    assert.equal(received.sessionId.length > 0, true);
    assert.equal(received.routingId, received.sessionId);
  } finally {
    client?.destroy();
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
  attachActorGateway() {}
  async bindActor() {}
  async unbindActor() {}
  sendBoundActor() { return true; }
}

function fakeMessage(text) {
  return {
    closed: false,
    getString() {
      return text;
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

function withTimeout(promise, timeoutMs, label) {
  let timeout;
  const guard = new Promise((_, reject) => {
    timeout = setTimeout(() => reject(new Error(`${label} timed out`)), timeoutMs);
  });
  return Promise.race([promise, guard]).finally(() => clearTimeout(timeout));
}

function streamHeaderJson(overrides) {
  return {
    kind: connector.ZlinkStreamMessageKind.Send,
    codec: connector.ZlinkStreamCodec.Json,
    flags: overrides.requestSeq === undefined
      ? connector.ZlinkStreamHeaderFlags.None
      : connector.ZlinkStreamHeaderFlags.HasRequestSeq,
    name: 'Packet',
    metadata: { values: [] },
    ...overrides,
    requestSeq: overrides.requestSeq?.toString()
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
